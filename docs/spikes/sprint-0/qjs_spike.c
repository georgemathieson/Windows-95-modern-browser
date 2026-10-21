#include <stdio.h>
#include <string.h>
#include "quickjs.h"
int main(void) {
  JSRuntime *rt = JS_NewRuntime();
  JSContext *ctx = JS_NewContext(rt);
  const char *src =
    "const fib = n => n < 2 ? n : fib(n-1) + fib(n-2);"
    "class P { #x = 1; get x() { return this.#x; } };"
    "const o = { a: [1,2,3].map(v => v ** 2), b: new P().x, c: `t${'ok'}` };"
    "const s = JSON.stringify(o) + ' ' + fib(20) + ' ' + (2n ** 64n) + ' ' + /(?<y>\\d{4})/.exec('in 1995').groups.y"
    " + ' ' + [3,1,2].toSorted().join('') + ' ' + Math.hypot(3,4) + ' ' + (0.1+0.2).toFixed(10) + ' ' + new Date(0).toISOString();"
    "s;";
  JSValue v = JS_Eval(ctx, src, strlen(src), "<spike>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(v)) {
    JSValue e = JS_GetException(ctx); const char *m = JS_ToCString(ctx, e);
    printf("EXCEPTION: %s\n", m); JS_FreeCString(ctx, m); JS_FreeValue(ctx, e); return 1;
  }
  const char *r = JS_ToCString(ctx, v);
  printf("%s\n", r);
  JS_FreeCString(ctx, r); JS_FreeValue(ctx, v);
  JS_FreeContext(ctx); JS_FreeRuntime(rt);
  return 0;
}
