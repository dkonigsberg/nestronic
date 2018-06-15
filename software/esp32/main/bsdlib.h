#ifndef BSDLIB_H
#define BSDLIB_H

#include <sys/cdefs.h>

void qsort(void *, size_t, size_t,
        int (*)(const void *, const void *));
void qsort_r(void *, size_t, size_t, void *,
        int (*)(void *, const void *, const void *));

int scandir(const char *, struct dirent ***,
        int (*)(const struct dirent *), int (*)(const struct dirent **,
        const struct dirent **));
int alphasort(const struct dirent **d1, const struct dirent **d2);

#endif /* BSDLIB_H */
