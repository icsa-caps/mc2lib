/*
 * Copyright (c) 2014, Marco Elver
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of the software nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef MC2LIB_CODEGEN_COMPILER_HPP_
#define MC2LIB_CODEGEN_COMPILER_HPP_

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../memconsistency/model14.hpp"

namespace mc2lib {
namespace codegen {

namespace mc = memconsistency;

class AssemblerState;
class Operation;

typedef mc::Event::Addr InstPtr;
typedef std::shared_ptr<Operation> OperationPtr;
typedef std::unordered_map<mc::Iiid::Pid, std::vector<Operation*>> Threads;
typedef uint8_t WriteID;

class Operation {
  public:
    explicit Operation(mc::Iiid::Pid pid)
        : pid_(pid)
    {}

    virtual ~Operation()
    {}

    /**
     * Provide Reset, as emit functions may modify the state of an Operation to
     * store information to map instructions to events.
     */
    virtual void reset() = 0;

    /**
     * Emit X86-64 machine code; fill in architecture-dependent ordering
     * relations.
     *
     * @param asms Pointer to AssemblerState instance of calling Compiler.
     * @param arch Pointer to target memory consistency model Architecture.
     * @param start Instruction pointer to first instruction when executing.
     * @param code Pointer to memory to be copied into.
     * @param len Maximum lenth of code.
     *
     * @return Size of emitted code.
     */
    virtual std::size_t emit_X86_64(AssemblerState *asms,
                                    mc::model14::Arch_TSO *arch, InstPtr start,
                                    void *code, std::size_t len)
    {
        // Provide default (nop), as not all operations may be implementable on
        // all architectures.
        return 0;
    }

    /**
     * Generate static program-order relation.
     *
     * @param asms Pointer to AssemblerState instance maintained by Compiler.
     * @param ew Pointer to ExecWitness to be inserted into.
     * @param before Pointer to last event in program-order; nullptr if none exists.
     *
     * @return Last event in program-order generated by this operation.
     */
    virtual const mc::Event* insert_po(AssemblerState *asms,
                                       mc::model14::ExecWitness *ew,
                                       const mc::Event *before) const = 0;

    /**
     * Insert dynamic ordering relations (read-from, coherence-order).
     *
     * @param asms Pointer to AssemblerState instance maintained by Compiler.
     * @param ew Pointer to ExecWitness to be inserted into.
     * @param ip Instruction pointer of instruction for which a value was observed.
     * @param addr Address for observed operation.
     * @param from_id Pointer to observed memory (WriteIDs).
     * @param size Total size of observed memory operations in from_id;
     *             implementation should assert expected size.
     *
     * @return Success or not.
     */
    virtual bool insert_from(AssemblerState *asms, mc::model14::ExecWitness *ew,
                             InstPtr ip, mc::Event::Addr addr,
                             const WriteID *from_id, std::size_t size) const = 0;

    mc::Iiid::Pid pid() const
    { return pid_; }

    void set_pid(mc::Iiid::Pid pid)
    { pid_ = pid; }

  private:
    mc::Iiid::Pid pid_;
};

class AssemblerState {
  public:
    static constexpr std::size_t MAX_INST_SIZE = 8;
    static constexpr std::size_t MAX_INST_EVTS  = MAX_INST_SIZE / sizeof(WriteID);
    static constexpr WriteID INIT_WRITE = 0x00;
    static constexpr WriteID MIN_WRITE = INIT_WRITE + 1;
    static constexpr WriteID MAX_WRITE = 0xff - (MAX_INST_EVTS - 1);
    static constexpr mc::Iiid::Poi MIN_READ = 0x8000000000000000ULL;
    static constexpr mc::Iiid::Poi MAX_READ = 0xffffffffffffffffULL - (MAX_INST_EVTS - 1);

    explicit AssemblerState(mc::model14::ExecWitness *ew)
        : ew_(ew)
    {}

    void reset()
    {
        last_write_id_ = MIN_WRITE - 1;
        last_read_id_ = MIN_READ - 1;

        writes_.clear();
    }

    bool exhausted() const
    { return last_write_id_ >= MAX_WRITE || last_read_id_ >= MAX_READ; }

    template <std::size_t max_size, class Func>
    std::array<const mc::Event*, max_size/sizeof(WriteID)>
    make_event(mc::Iiid::Pid pid, mc::Event::Type type,
               mc::Event::Addr addr, std::size_t size, Func mkevt)
    {
        static_assert(max_size <= MAX_INST_SIZE, "Invalid size!");
        static_assert(sizeof(WriteID) <= max_size, "Invalid size!");
        static_assert(max_size % sizeof(WriteID) == 0, "Invalid size!");
        assert(size <= max_size);
        assert(sizeof(WriteID) <= size);
        assert(size % sizeof(WriteID) == 0);

        assert(!exhausted());

        std::array<const mc::Event*, max_size/sizeof(WriteID)> result;

        for (std::size_t i = 0; i < size/sizeof(WriteID); ++i) {
            result[i] = mkevt(i * sizeof(WriteID));
        }

        return result;
    }

    template <std::size_t max_size>
    std::array<const mc::Event*, max_size/sizeof(WriteID)>
    make_read(mc::Iiid::Pid pid, mc::Event::Type type, mc::Event::Addr addr,
              std::size_t size = max_size)
    {
        return make_event<max_size>(pid, type, addr, size, [&](mc::Event::Addr offset) {
            const mc::Event event =
                mc::Event(type, addr + offset, mc::Iiid(pid, ++last_read_id_));

            return &ew_->events.insert(event, true);
        });
    }

    template <std::size_t max_size>
    std::array<const mc::Event*, max_size/sizeof(WriteID)>
    make_write(mc::Iiid::Pid pid, mc::Event::Type type, mc::Event::Addr addr,
               WriteID *data, std::size_t size = max_size)
    {
        return make_event<max_size>(pid, type, addr, size, [&](mc::Event::Addr offset) {
            const WriteID write_id = ++last_write_id_;

            const mc::Event event =
                mc::Event(type, addr + offset, mc::Iiid(pid, write_id));

            *(data + offset) = write_id;
            return (writes_[write_id] = &ew_->events.insert(event, true));
        });
    }

    template <std::size_t max_size>
    std::array<const mc::Event*, max_size/sizeof(WriteID)>
    get_write(const mc::Event *after, mc::Event::Addr addr,
              const WriteID *from_id, std::size_t size = max_size)
    {
        static_assert(max_size <= MAX_INST_SIZE, "Invalid size!");
        static_assert(sizeof(WriteID) <= max_size, "Invalid size!");
        static_assert(max_size % sizeof(WriteID) == 0, "Invalid size!");
        assert(size <= max_size);
        assert(sizeof(WriteID) <= size);
        assert(size % sizeof(WriteID) == 0);

        assert(after != nullptr);

        std::array<const mc::Event*, max_size/sizeof(WriteID)> result;

        for (std::size_t i = 0; i < size/sizeof(WriteID); ++i) {
            WriteID_EventPtr::const_iterator write;

            const bool valid = from_id[i] != INIT_WRITE &&
                               (write = writes_.find(from_id[i])) != writes_.end() &&
                               write->second->addr == addr &&
                               write->second->iiid != after->iiid;
            if (valid) {
                result[i] = write->second;
            } else {
                if (from_id[i] != INIT_WRITE) {
                    // While the checker works even if memory is not 0'ed out
                    // completely, as the chances of reading a write-id from a
                    // previous epoch that has already been used in this epoch is
                    // low and doesn't necessarily cause a false positive, it is
                    // recommended that memory is 0'ed out for every new epoch.
                    std::cerr << "warn: Invalid write, but not INIT_WRITE! "
                              << "Has memory been reset?" << std::endl;
                }

                auto initial = mc::Event(mc::Event::Write, addr, mc::Iiid(-1, addr));
                result[i] = &ew_->events.insert(initial);
            }

            addr += sizeof(WriteID);
        }

        return result;
    }

  private:
    typedef std::unordered_map<WriteID, const mc::Event*> WriteID_EventPtr;

    mc::model14::ExecWitness *ew_;

    WriteID_EventPtr writes_;

    WriteID last_write_id_;
    mc::Iiid::Poi last_read_id_;

};

template <class Backend>
class Compiler {
  public:
    explicit Compiler(mc::model14::Architecture *arch, mc::model14::ExecWitness *ew,
                      const Threads *threads = nullptr)
        : asms_(ew), backend_(&asms_, arch), ew_(ew)
    {
        reset(threads);
    }

    void reset(const Threads *threads = nullptr)
    {
        threads_ = threads;
        asms_.reset();
        backend_.reset();
        ew_->clear();
        ip_to_op_.clear();
    }

    std::size_t emit(Operation *op, InstPtr base, void *code, std::size_t len,
                     const mc::Event **last_evt) {
        // Generate code and architecture-specific ordering relations.
        const std::size_t op_len = backend_(op, base, code, len);
        assert(op_len != 0);

        // Insert IP to Operation mapping.
        assert(ip_to_op_.find(base) == ip_to_op_.end());
        ip_to_op_[base] = std::make_pair(base + op_len, op);

        // Generate program-order.
        if (last_evt != nullptr) {
            *last_evt = op->insert_po(&asms_, ew_, *last_evt);
        } else {
            op->insert_po(&asms_, ew_, nullptr);
        }

        return op_len;
    }

    std::size_t emit(mc::Iiid::Pid pid, InstPtr base, void *code, std::size_t len)
    {
        assert(threads_ != nullptr);

        auto thread = threads_->find(pid);

        if (thread == threads_->end()) {
            return 0;
        }

        std::size_t emit_len = 0;
        const mc::Event *last_evt = nullptr;

        for (const auto& op : thread->second) {
            // Generate code and architecture-specific ordering relations.
            const std::size_t op_len = emit(op, base + emit_len, code,
                                            len - emit_len, &last_evt);

            emit_len += op_len;
            assert(emit_len <= len);

            code = static_cast<char*>(code) + op_len;
        }

        return emit_len;
    }

    bool insert_from(InstPtr ip, mc::Event::Addr addr,
                     const WriteID *from_id, std::size_t size)
    {
        return ip_to_op(ip)->insert_from(&asms_, ew_, ip, addr, from_id, size);
    }

    const Operation* ip_to_op(InstPtr ip)
    {
        assert(!ip_to_op_.empty());

        auto e = --ip_to_op_.upper_bound(ip);
        assert(e->first <= ip && ip < e->second.first);

        return e->second.second;
    }

  private:
    typedef std::map<InstPtr, std::pair<InstPtr, const Operation*>> InstPtr_Op;

    AssemblerState asms_;
    Backend backend_;
    const Threads *threads_;

    mc::model14::ExecWitness *ew_;

    // Each processor executes unique code, hence IP must be unique.  Only
    // stores the start IP of Op-sequence
    InstPtr_Op ip_to_op_;
};

class Backend_X86_64 {
  public:
    explicit Backend_X86_64(AssemblerState *asms,
                            mc::model14::Architecture *arch)
        : asms_(asms), arch_(dynamic_cast<mc::model14::Arch_TSO*>(arch))
    {
        assert(arch_ != nullptr);
    }

    void reset()
    {
        arch_->clear();
    }

    std::size_t operator ()(Operation *op, InstPtr start,
                            void *code, std::size_t len) const
    {
        return op->emit_X86_64(asms_, arch_, start, code, len);
    }

  private:
    AssemblerState *asms_;
    mc::model14::Arch_TSO *arch_;
};

template <class T>
inline Threads extract_threads(const T& container)
{
    Threads result;

    for (const auto& op : container) {
        op->reset();
        result[op->pid()].emplace_back(&(*op));
    }

    return result;
}

} /* namespace codegen */
} /* namespace mc2lib */

#endif /* MC2LIB_CODEGEN_COMPILER_HPP_ */

/* vim: set ts=4 sts=4 sw=4 et : */
