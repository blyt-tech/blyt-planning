/* Stage 5 entry point — under the Lua-direct WASM build, the WASM module's
 * main() simply invokes the same fc_console_main() runtime loop the
 * emulator and native targets use. */
extern void fc_console_main(void);
int main(void) { fc_console_main(); return 0; }
