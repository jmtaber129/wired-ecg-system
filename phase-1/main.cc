#include "uart-queue.h"

#include <msp430g2553.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>

#define TXLED BIT0
#define RXLED BIT6
#define TXD BIT2
#define RXD BIT1

const char* kEot = "\004\n";

enum UartCommand {
  kStart = 'a',
  kStop = 'b'
};

void ConfigureClocks();
void ConfigurePorts();
void ConfigureUart();
void ConfigureTimer();

volatile UartCommand current_command = kStop;

UartQueue uart_queue;

int main(void) {
  ConfigureClocks();
  ConfigurePorts();
  ConfigureUart();
  ConfigureTimer();

  __bis_SR_register(GIE);

  // Serial data-streaming loop.
  while (1) {
    if (!uart_queue.Empty()) {
      // Get the next char to send.
      char current_char = uart_queue.Front();

      // Wait for the transmit buffer to be ready.
      while (!(IFG2 & UCA0TXIFG));

      // Load the char into the buffer.
      UCA0TXBUF = current_char;

      // Remove the string we just sent from the queue.
      uart_queue.Pop();
    } else {
      // If there's nothing left to process, enter low-power mode.
      __bis_SR_register(LPM0_bits);
    }

  }
}

void ConfigureClocks() {
  WDTCTL = WDTPW + WDTHOLD; // Stop WDT.
  DCOCTL = CALDCO_1MHZ;
  BCSCTL1 = CALBC1_1MHZ; // Set DCO.
}

void ConfigurePorts() {
  P2DIR |= 0xFF; // All P2.x outputs.
  P2OUT &= 0x00; // All P2.x reset.
  P1SEL |= RXD + TXD ; // P1.1 = RXD, P1.2=TXD.
  P1SEL2 |= RXD + TXD ; // P1.1 = RXD, P1.2=TXD.
  P1DIR |= RXLED + TXLED;
  P1OUT &= 0x00;
}

void ConfigureUart() {
  UCA0CTL1 |= UCSSEL_2; // SMCLK.
  UCA0BR0 = 0x08; // 1MHz 115200.
  UCA0BR1 = 0x00; // 1MHz 115200.
  UCA0MCTL = UCBRS2 + UCBRS0; // Modulation UCBRSx = 5.
  UCA0CTL1 &= ~UCSWRST; // Initialize USCI state machine.
  UC0IE |= UCA0RXIE; // Enable USCI_A0 RX interrupt.
}

void ConfigureTimer() {
  // Configure timer for 360Hz.
  TA1CCR0 = 2778;  // Generate an interrupt every 2.778ms.
  TA1CCTL0 = CCIE;
  TA1CTL = TASSEL_2 + ID_0 + MC_1;
}

#pragma vector=USCIAB0RX_VECTOR
__interrupt void USCI0RX_ISR(void) {
  if (UCA0RXBUF == kStart) {
    current_command = kStart;
  } else if (UCA0RXBUF == kStop) {
    current_command = kStop;

    // Add end of transmission (EOT) character to the queue.
    uart_queue.Push(kEot);

    // Wake up from LPM so the EOT char can be transmitted.
    LPM0_EXIT;
  } else {
    // Received character isn't a valid command.
    // TODO(jmtaber129): Add invalid command error handling.
  }
}

#pragma vector=TIMER1_A0_VECTOR
__interrupt void Timer_A (void) {
  if (current_command == kStop) {
    // If the current command is STOP, don't take any measurements, and just
    // return.
    return;
  }

  // Toggle P1.0 for debugging purposes.
  P1OUT ^= BIT0;

  // TODO(jmtaber129): Read from the analog pin, serialize the reading, and add
  // the serialized reading to the queue.

  char buffer[10];
  buffer[0] = 'a';
  buffer[1] = '\n';
  buffer[2] = '\0';
  uart_queue.Push(buffer);

  // The string that was just pushed to the queue needs to be sent by the main
  // loop, so exit LPM.
  LPM0_EXIT;

  return;
}
