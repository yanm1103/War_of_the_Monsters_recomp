#ifndef MEMORY_STACK_H
#define MEMORY_STACK_H

/* Linear allocator with a mark chain: every mark stores the previous mark in the word it occupies. */
class MemoryStack {
public:
    void *start;
    char *low;
    int mark;
    char *end;
    char *hi1;
    char *hi2;

    static MemoryStack global;

    MemoryStack(void *mem, unsigned size);
    void clear(void);

    /* Pushes a mark at the low end (inlined everywhere in retail). */
    void pushMark(void)
    {
        int *q = (int *)(((int)low + 3) & ~3);
        int old = mark;

        mark = (int)q;
        low = (char *)(q + 1);
        *q = old;
    }

    /* Allocates 16-byte aligned memory at the low end. */
    void *alloc16(unsigned short size)
    {
        char *p = (char *)(((int)low + 15) & ~15);

        low = p + size;
        return p;
    }

    /* Pops back to the last mark. */
    void popMark(void)
    {
        int *q = (int *)mark;

        low = (char *)q;
        mark = *q;
    }
};

#endif
