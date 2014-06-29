#define _GNU_SOURCE /* Required for asprintf */
#include <stdio.h>
#include <stdlib.h>

/* Return a newly malloc'd greeting string */
char* generate_message(const char* name)
{
    char* s = NULL;
    asprintf(&s, "Hello, %s",name);
    return s;
}

int main(int argc, char** argv)
{
    char* s = generate_message("world");
    printf("%s\n",s);
    free(s);
    return EXIT_SUCCESS;
}
