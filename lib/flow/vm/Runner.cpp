#include <x0/flow/vm/Runner.h>
#include <x0/flow/vm/Params.h>
#include <x0/flow/vm/NativeCallback.h>
#include <x0/flow/vm/Handler.h>
#include <x0/flow/vm/Program.h>
#include <x0/flow/vm/Match.h>
#include <x0/flow/vm/Instruction.h>
#include <vector>
#include <utility>
#include <memory>
#include <new>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace x0 {
namespace FlowVM {

std::unique_ptr<Runner> Runner::create(Handler* handler)
{
    Runner* p = (Runner*) malloc(sizeof(Runner) + handler->registerCount() * sizeof(uint64_t));
    new (p) Runner(handler);
    return std::unique_ptr<Runner>(p);
}

static FlowString* t = nullptr;

Runner::Runner(Handler* handler) :
    handler_(handler),
    program_(handler->program()),
    userdata_(nullptr),
    stringGarbage_(),
    garbage_()
{
    // initialize emptyString()
    t = newString("");

    // initialize registers
    memset(data_, 0, sizeof(Register) * handler_->registerCount());
}

Runner::~Runner()
{
    for (Object* obj: garbage_) {
        delete obj;
    }
}

void Runner::operator delete (void* p)
{
    free(p);
}

FlowString* Runner::newString(const std::string& value)
{
    stringGarbage_.push_back(Buffer(value.c_str(), value.size()));
    return &stringGarbage_.back();
}

FlowString* Runner::newString(const char* p, size_t n)
{
    stringGarbage_.push_back(Buffer(p, n));
    return &stringGarbage_.back();
}

FlowString* Runner::catString(const FlowString& a, const FlowString& b)
{
    Buffer s(a.size() + b.size() + 1);
    s.push_back(a);
    s.push_back(b);

    stringGarbage_.push_back(std::move(s));

    return &stringGarbage_.back();
}

bool Runner::run()
{
    const Program* program = handler_->program();
    const auto& code = handler_->code();
    const Instruction* pc = code.data();
    uint64_t ticks = 0;

    #define OP opcode(*pc)
    #define A  operandA(*pc)
    #define B  operandB(*pc)
    #define C  operandC(*pc)

    #define toString(R)     (*(FlowString*) data_[R])
    #define toIPAddress(R)  (*(IPAddress*) data_[R])
    #define toCidr(R)       (*(Cidr*) data_[R])
    #define toRegExp(R)     (*(RegExp*) data_[R])
    #define toNumber(R)     ((FlowNumber) data_[R])

    #define toStringPtr(R)  ((FlowString*) data_[R])
    #define toCidrPtr(R)    ((Cidr*) data_[R])

    #define instr(name) \
        l_##name: \
        /*disassemble(*pc, pc - code.data());*/ \
        ++ticks;

    #define next goto *ops[opcode(*++pc)]

    // {{{ jump table
    static const void* ops[] = {
        [Opcode::NOP]      = &&l_nop,

        // control
        [Opcode::EXIT]      = &&l_exit,
        [Opcode::JMP]       = &&l_jmp,
        [Opcode::JN]        = &&l_jn,
        [Opcode::JZ]        = &&l_jz,

        // debug
        [Opcode::NTICKS]    = &&l_nticks,
        [Opcode::NDUMPN]    = &&l_ndumpn,

        // copy
        [Opcode::MOV]       = &&l_mov,

        // numerical
        [Opcode::IMOV]      = &&l_imov,
        [Opcode::NCONST]    = &&l_nconst,
        [Opcode::NNEG]      = &&l_nneg,
        [Opcode::NADD]      = &&l_nadd,
        [Opcode::NSUB]      = &&l_nsub,
        [Opcode::NMUL]      = &&l_nmul,
        [Opcode::NDIV]      = &&l_ndiv,
        [Opcode::NREM]      = &&l_nrem,
        [Opcode::NSHL]      = &&l_nshl,
        [Opcode::NSHR]      = &&l_nshr,
        [Opcode::NPOW]      = &&l_npow,
        [Opcode::NAND]      = &&l_nand,
        [Opcode::NOR]       = &&l_nor,
        [Opcode::NXOR]      = &&l_nxor,
        [Opcode::NCMPZ]     = &&l_ncmpz,
        [Opcode::NCMPEQ]    = &&l_ncmpeq,
        [Opcode::NCMPNE]    = &&l_ncmpne,
        [Opcode::NCMPLE]    = &&l_ncmple,
        [Opcode::NCMPGE]    = &&l_ncmpge,
        [Opcode::NCMPLT]    = &&l_ncmplt,
        [Opcode::NCMPGT]    = &&l_ncmpgt,

        // boolean op
        [Opcode::BNOT]      = &&l_bnot,
        [Opcode::BAND]      = &&l_band,
        [Opcode::BOR]       = &&l_bor,
        [Opcode::BXOR]      = &&l_bxor,

        // string op
        [Opcode::SCONST]    = &&l_sconst,
        [Opcode::SADD]      = &&l_sadd,
        [Opcode::SSUBSTR]   = &&l_ssubstr,
        [Opcode::SCMPEQ]    = &&l_scmpeq,
        [Opcode::SCMPNE]    = &&l_scmpne,
        [Opcode::SCMPLE]    = &&l_scmple,
        [Opcode::SCMPGE]    = &&l_scmpge,
        [Opcode::SCMPLT]    = &&l_scmplt,
        [Opcode::SCMPGT]    = &&l_scmpgt,
        [Opcode::SCMPBEG]   = &&l_scmpbeg,
        [Opcode::SCMPEND]   = &&l_scmpend,
        [Opcode::SCONTAINS] = &&l_scontains,
        [Opcode::SLEN]      = &&l_slen,
        [Opcode::SISEMPTY]  = &&l_sisempty,
        [Opcode::SPRINT]    = &&l_sprint,
        [Opcode::SMATCHEQ]  = &&l_smatch,
        [Opcode::SMATCHBEG] = &&l_smatch,
        [Opcode::SMATCHEND] = &&l_smatch,
        [Opcode::SMATCHR]   = &&l_smatch,

        // ipaddr
        [Opcode::PCONST]    = &&l_pconst,
        [Opcode::PCMPEQ]    = &&l_pcmpeq,
        [Opcode::PCMPNE]    = &&l_pcmpne,
        [Opcode::PINCIDR]   = &&l_pincidr,

        // cidr
        [Opcode::CCONST]    = &&l_cconst,

        // regex
        [Opcode::SREGMATCH] = &&l_sregmatch,
        [Opcode::SREGGROUP] = &&l_sreggroup,

        // conversion
        [Opcode::S2I] = &&l_s2i,
        [Opcode::I2S] = &&l_i2s,
        [Opcode::P2S] = &&l_p2s,
        [Opcode::C2S] = &&l_c2s,
        [Opcode::R2S] = &&l_r2s,
        [Opcode::SURLENC] = &&l_surlenc,
        [Opcode::SURLDEC] = &&l_surldec,

        // arrays
        [Opcode::ASNEW] = &&l_asnew,
        [Opcode::ASINIT] = &&l_asinit,

        [Opcode::ANNEW] = &&l_annew,
        [Opcode::ANINIT] = &&l_aninit,
        [Opcode::ANINITI] = &&l_aniniti,

        // invokation
        [Opcode::CALL] = &&l_call,
        [Opcode::HANDLER] = &&l_handler,
    };
    // }}}

    goto *ops[opcode(*pc)];

    // {{{ misc
    instr (nop) {
        next;
    }
    // }}}
    // {{{ control
    instr (exit) {
        return A != 0;
    }

    instr (jmp) {
        pc = code.data() + A;
        goto *ops[OP];
    }

    instr (jn) {
        if (data_[A] != 0) {
            pc = code.data() + B;
            goto *ops[OP];
        } else {
            next;
        }
    }

    instr (jz) {
        if (data_[A] == 0) {
            pc = code.data() + B;
            goto *ops[OP];
        } else {
            next;
        }
    }
    // }}}
    // {{{ copy
    instr (mov) {
        data_[A] = data_[B];
        next;
    }
    // }}}
    // {{{ debug
    instr (nticks) {
        data_[A] = ticks;
        next;
    }

    instr (ndumpn) {
        printf("regdump: ");
        for (int i = 0; i < B; ++i) {
            if (i) printf(", ");
            printf("r%d = %li", A + i, (int64_t)data_[A + i]);
        }
        if (B) printf("\n");
        next;
    }
    // }}}
    // {{{ numerical
    instr (imov) {
        data_[A] = B;
        next;
    }

    instr (nconst) {
        data_[A] = program->numbers()[B];
        next;
    }

    instr (nneg) {
        data_[A] = (Register) (-toNumber(B));
        next;
    }

    instr (nadd) {
        data_[A] = static_cast<Register>(toNumber(B) + toNumber(C));
        next;
    }

    instr (nsub) {
        data_[A] = static_cast<Register>(toNumber(B) - toNumber(C));
        next;
    }

    instr (nmul) {
        data_[A] = static_cast<Register>(toNumber(B) * toNumber(C));
        next;
    }

    instr (ndiv) {
        data_[A] = static_cast<Register>(toNumber(B) / toNumber(C));
        next;
    }

    instr (nrem) {
        data_[A] = static_cast<Register>(toNumber(B) % toNumber(C));
        next;
    }

    instr (nshl) {
        data_[A] = static_cast<Register>(toNumber(B) << toNumber(C));
        next;
    }

    instr (nshr) {
        data_[A] = static_cast<Register>(toNumber(B) >> toNumber(C));
        next;
    }

    instr (npow) {
        data_[A] = static_cast<Register>(powl(toNumber(B), toNumber(C)));
        next;
    }

    instr (nand) {
        data_[A] = data_[B] & data_[C];
        next;
    }

    instr (nor) {
        data_[A] = data_[B] | data_[C];
        next;
    }

    instr (nxor) {
        data_[A] = data_[B] ^ data_[C];
        next;
    }

    instr (ncmpz) {
        data_[A] = static_cast<Register>(toNumber(B) == 0);
        next;
    }

    instr (ncmpeq) {
        data_[A] = static_cast<Register>(toNumber(B) == toNumber(C));
        next;
    }

    instr (ncmpne) {
        data_[A] = static_cast<Register>(toNumber(B) != toNumber(C));
        next;
    }

    instr (ncmple) {
        data_[A] = static_cast<Register>(toNumber(B) <= toNumber(C));
        next;
    }

    instr (ncmpge) {
        data_[A] = static_cast<Register>(toNumber(B) >= toNumber(C));
        next;
    }

    instr (ncmplt) {
        data_[A] = static_cast<Register>(toNumber(B) < toNumber(C));
        next;
    }

    instr (ncmpgt) {
        data_[A] = static_cast<Register>(toNumber(B) > toNumber(C));
        next;
    }
    // }}}
    // {{{ boolean
    instr (bnot) {
        data_[A] = (Register) (!toNumber(B));
        next;
    }

    instr (band) {
        data_[A] = toNumber(B) && toNumber(C);
        next;
    }

    instr (bor) {
        data_[A] = toNumber(B) || toNumber(C);
        next;
    }

    instr (bxor) {
        data_[A] = toNumber(B) ^ toNumber(C);
        next;
    }

    // }}}
    // {{{ string
    instr (sconst) { // A = stringConstTable[B]
        data_[A] = (Register) &program->string(B);
        next;
    }

    instr (sadd) { // A = concat(B, C)
        data_[A] = (Register) catString(toString(B), toString(C));
        next;
    }

    instr (ssubstr) { // A = substr(B, C /*offset*/, C+1 /*count*/)
        data_[A] = (Register) newString(toString(B).substr(data_[C], data_[C + 1]));
        next;
    }

    instr (scmpeq) {
        data_[A] = toString(B) == toString(C);
        next;
    }

    instr (scmpne) {
        data_[A] = toString(B) != toString(C);
        next;
    }

    instr (scmple) {
        data_[A] = toString(B) <= toString(C);
        next;
    }

    instr (scmpge) {
        data_[A] = toString(B) >= toString(C);
        next;
    }

    instr (scmplt) {
        data_[A] = toString(B) < toString(C);
        next;
    }

    instr (scmpgt) {
        data_[A] = toString(B) > toString(C);
        next;
    }

    instr (scmpbeg) {
        const auto& b = toString(B);
        const auto& c = toString(C);
        data_[A] = b.begins(c);
        next;
    }

    instr (scmpend) {
        const auto& b = toString(B);
        const auto& c = toString(C);
        data_[A] = b.ends(c);
        next;
    }

    instr (scontains) {
        data_[A] = toString(B).find(toString(C)) != FlowString::npos;
        next;
    }

    instr (slen) {
        data_[A] = toString(B).size();
        next;
    }

    instr (sisempty) {
        data_[A] = toString(B).empty();
        next;
    }

    instr (sprint) {
        printf("%s\n", toString(A).str().c_str());
        next;
    }

    instr (smatch) {
        auto result = program_->match(B)->evaluate(toStringPtr(A), this);
        pc = code.data() + result;
        goto *ops[OP];
    }
    // }}}
    // {{{ ipaddr
    instr (pconst) { // A = stringConstTable[B]
        data_[A] = (Register) &program->ipaddrs()[B];
        next;
    }

    instr (pcmpeq) {
        data_[A] = toIPAddress(B) == toIPAddress(C);
        next;
    }

    instr (pcmpne) {
        data_[A] = toIPAddress(B) != toIPAddress(C);
        next;
    }

    instr (pincidr) {
        const IPAddress& ipaddr = toIPAddress(B);
        const Cidr& cidr = toCidr(C);
        data_[A] = cidr.contains(ipaddr);
        next;
    }
    // }}}
    // {{{ cidr
    instr (cconst) {
        data_[A] = (Register) &program->cidr(B);
        next;
    }
    // }}}
    // {{{ regex
    instr (sregmatch) { // A = B =~ C
        RegExpContext* cx = (RegExpContext*) userdata();
        data_[A] = program_->regularExpression(C)->match(toString(B), cx ? cx->regexMatch() : nullptr);

        next;
    }

    instr (sreggroup) { // A = regex.group(B)
        FlowNumber position = toNumber(B);
        RegExpContext* cx = (RegExpContext*) userdata();
        RegExp::Result* rr = cx->regexMatch();
        const auto& match = rr->at(position);

        data_[A] = (Register) newString(match.first, match.second);

        next;
    }
    // }}}
    // {{{ conversion
    instr (s2i) { // A = atoi(B)
        data_[A] = toString(B).toInt();
        next;
    }

    instr (i2s) { // A = itoa(B)
        char buf[64];
        if (snprintf(buf, sizeof(buf), "%li", (int64_t) data_[B]) > 0) {
            data_[A] = (Register) newString(buf);
        } else {
            data_[A] = (Register) emptyString();
        }
        next;
    }

    instr (p2s) { // A = ip(B).toString()
        const IPAddress& ipaddr = toIPAddress(B);
        data_[A] = (Register) newString(ipaddr.str());
        next;
    }

    instr (c2s) { // A = cidr(B).toString()
        const Cidr& cidr = toCidr(B);
        data_[A] = (Register) newString(cidr.str());
        next;
    }

    instr (r2s) { // A = regex(B).toString()
        const RegExp& re = toRegExp(B);
        data_[A] = (Register) newString(re.pattern());
        next;
    }

    instr (surlenc) { // A = urlencode(B)
        // TODO
        next;
    }

    instr (surldec) { // B = urldecode(B)
        // TODO
        next;
    }
    // }}}
    // {{{ arrays
    instr (asnew) { // RI
        GCStringArray* array = new GCStringArray(B);
        garbage_.push_back(array);
        data_[A] = (Register) array;

        next;
    }

    instr (asinit) { // RIR
        GCStringArray* array = (GCStringArray*) data_[A];
        size_t index = B;
        const FlowString& value = toString(C);

        array->data()[index] = value;

        next;
    }

    // ANEW(array, size)
    instr (annew) { // RI
        GCIntArray* array = new GCIntArray(B);
        garbage_.push_back(array);
        data_[A] = (Register) array;

        next;
    }

    // ANINIT(array, index, value)
    instr (aninit) { // RIR
        GCIntArray* array = (GCIntArray*) data_[A];
        size_t index = B;
        FlowNumber value = toNumber(C);

        array->data()[index] = value;

        next;
    }

    // ANINITI(array, index, value)
    instr (aniniti) { // RII
        GCIntArray* array = (GCIntArray*) data_[A];
        size_t index = B;
        FlowNumber value = C;

        array->data()[index] = value;

        next;
    }
    // }}}
    // {{{ invokation
    instr (call) { // IIR
        size_t id = A;
        int argc = B;
        Register* argv = &data_[C];

        Params args(argc, argv, this);
        handler_->program()->nativeFunction(id)->invoke(args);

        next;
    }

    instr (handler) { // IIR
        size_t id = A;
        int argc = B;
        Value* argv = &data_[C];

        Params args(argc, argv, this);
        handler_->program()->nativeHandler(id)->invoke(args);
        const bool handled = (bool) argv[0];

        if (handled) {
            return true;
        }

        next;
    }
    // }}}
}

} // namespace FlowVM
} // namespace x0
