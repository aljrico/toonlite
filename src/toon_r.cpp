#include "toon_r.h"

namespace toonlite {

namespace {

// One continuation token, preserved for the session (the cpp11 approach).
SEXP unwind_token() {
    static SEXP token = [] {
        SEXP t = R_MakeUnwindCont();
        R_PreserveObject(t);
        return t;
    }();
    return token;
}

[[noreturn]] void unwind_cleanup_jump(void* jmpbuf) {
    std::longjmp(*static_cast<std::jmp_buf*>(jmpbuf), 1);
}

void unwind_cleanup(void* jmpbuf, Rboolean jump) {
    if (jump == TRUE) unwind_cleanup_jump(jmpbuf);
}

// Run `fun(data)` under R_UnwindProtect. If R jumps (error, interrupt,
// restart), control comes back here and the jump continues as a C++
// exception; the caller's catch resumes it with R_ContinueUnwind().
SEXP unwind_protect(SEXP (*fun)(void*), void* data) {
    SEXP token = unwind_token();
    SETCAR(token, R_NilValue);
    std::jmp_buf jmpbuf;
    if (setjmp(jmpbuf)) {
        throw UnwindException{token};
    }
    return R_UnwindProtect(fun, data, unwind_cleanup, &jmpbuf, token);
}

struct Call {
    SEXP fn;
    SEXP arg;
};

SEXP eval_call(void* data) {
    const Call* c = static_cast<const Call*>(data);
    SEXP call = PROTECT(Rf_lang2(c->fn, c->arg));
    SEXP res = Rf_eval(call, R_GlobalEnv);
    UNPROTECT(1);
    return res;
}

SEXP check_user_interrupt(void*) {
    R_CheckUserInterrupt();
    return R_NilValue;
}

}  // namespace

SEXP call_r_function(SEXP fn, SEXP arg) {
    Call c{fn, arg};
    return unwind_protect(eval_call, &c);
}

void check_interrupt() {
    unwind_protect(check_user_interrupt, nullptr);
}

}  // namespace toonlite
