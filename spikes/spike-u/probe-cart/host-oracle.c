#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int32_t f_probe(int n){
    float a=(float)n*1.5f; float b=a/3.0f-0.25f; float c=__builtin_fabsf(b);
    float d=(c>1.0f)?c:-c; float e=__builtin_copysignf(d,a);
    int32_t bits; __builtin_memcpy(&bits,&e,4); int32_t trunc=(int32_t)d;
    return bits ^ (int32_t)(trunc*2654435761u);
}
static int32_t d_probe(int n){
    double a=(double)n*1.5; double b=a/3.0-0.25; double c=__builtin_fabs(b);
    double d=(c>1.0)?c:-c; double e=__builtin_copysign(d,a);
    int64_t bits; __builtin_memcpy(&bits,&e,8); int32_t trunc=(int32_t)d;
    float f=(float)e; int32_t fbits; __builtin_memcpy(&fbits,&f,4);
    return (int32_t)bits ^ (int32_t)(bits>>32) ^ (int32_t)(trunc*2654435761u) ^ fbits;
}
int main(void){ for(int n=10;n<=30;n+=10) printf("n=%d fprobe=%d dprobe=%d\n",n,f_probe(n),d_probe(n)); }
