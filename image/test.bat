tcc -nostdlib -I/include -L/lib /lib/userlib_tcc.c hello.c -o hello.exe
tcc -nostdlib -I/include -L/lib /lib/userlib_tcc.c shell.c -o shell_test.exe
./hello.exe