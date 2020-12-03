#ifndef INC_UART_H_
#define INC_UART_H_

void uart_init();
char uart_intr();
void uart_putchar(int);
char uart_getchar();

#endif  // INC_UART_H_
