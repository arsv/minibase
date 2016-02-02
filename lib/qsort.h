typedef int (*qcmp)(const void* a, const void* b, long p);
void qsort(void* base, size_t nmemb, size_t size, qcmp cmp, long data);
