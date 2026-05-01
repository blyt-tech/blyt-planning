/* Stub double-float libgcc helpers.
 * The spike-c test (1+1) uses only integer arithmetic.
 * These stubs satisfy the linker without requiring a PIC rebuild of softfloat.a. */

double __adddf3(double a, double b)              { (void)a; (void)b; return 0.0; }
double __subdf3(double a, double b)              { (void)a; (void)b; return 0.0; }
double __muldf3(double a, double b)              { (void)a; (void)b; return 0.0; }
double __divdf3(double a, double b)              { (void)a; (void)b; return 0.0; }
double __negdf2(double a)                        { (void)a; return 0.0; }
int    __ltdf2 (double a, double b)              { (void)a; (void)b; return 0; }
int    __gtdf2 (double a, double b)              { (void)a; (void)b; return 0; }
int    __ledf2 (double a, double b)              { (void)a; (void)b; return 0; }
int    __gedf2 (double a, double b)              { (void)a; (void)b; return 0; }
int    __eqdf2 (double a, double b)              { (void)a; (void)b; return 0; }
int    __nedf2 (double a, double b)              { (void)a; (void)b; return 0; }
int    __unorddf2(double a, double b)            { (void)a; (void)b; return 0; }
double __floatsidf(int a)                        { (void)a; return 0.0; }
double __floatunsidf(unsigned a)                 { (void)a; return 0.0; }
double __floatdidf(long long a)                  { (void)a; return 0.0; }
double __floatundidf(unsigned long long a)       { (void)a; return 0.0; }
int    __fixdfsi(double a)                       { (void)a; return 0; }
unsigned __fixunsdfsi(double a)                  { (void)a; return 0; }
long long __fixdfdi(double a)                    { (void)a; return 0; }
unsigned long long __fixunsdfdi(double a)        { (void)a; return 0; }
double __extendsfdf2(float a)                    { (void)a; return 0.0; }
float  __truncdfsf2(double a)                    { (void)a; return 0.0f; }
