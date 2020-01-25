#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third-party/quickjs.h"

#define BUF_SIZE 1024

int main() {
    char buffer[BUF_SIZE];
    size_t size = 1;
    char *content = malloc(sizeof(char) * BUF_SIZE);
    content[0] = '\0';
    while(fgets(buffer, BUF_SIZE, stdin)) {
        char *old = content;
        size += strlen(buffer);
        content = realloc(content, size);
        strcat(content, buffer);
    }

    JSRuntime* runtime = JS_NewRuntime();
    JSContext* context = JS_NewContext(runtime);
    JSValue value = JS_Eval(context, content, strlen(content), "<input>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) {
        JSValue exception = JS_GetException(context);
        const char* str = JS_ToCString(context, exception);
        printf("Result: %s\n", str);
        JS_FreeCString(context, str);
        JS_FreeValue(context, exception);
    } else {
        const char* str = JS_ToCString(context, value);
        printf("Result: %s\n", str);
        JS_FreeCString(context, str);
    }
    JS_FreeValue(context, value);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    free(content);
    return 0;
}
