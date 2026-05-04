extern void fc_console_print(const char *s);
extern int snprintf(char *, unsigned long, const char *, ...);
extern int mylib_value(void);

__attribute__((section(".cart.info")))
static const char cart_info_stub[] = "FC32";

__attribute__((section(".cart.config")))
static const char cart_config_stub[] = "CF32";

static int frame = 0;

void fc_cart_init(void)   { frame = 0; }
void fc_cart_update(void) { frame++; }
void fc_cart_draw(void) {
    char buf[48];
    snprintf(buf, sizeof(buf), "frame %d mylib=%d\n", frame - 1, mylib_value());
    fc_console_print(buf);
}
