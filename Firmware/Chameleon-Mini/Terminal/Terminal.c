
#include "Terminal.h"
#include "../System.h"
#include "../LEDHook.h"
#ifdef CHAMELEON_TINY_UART_MODE
#include "../Uart.h"
#endif

#include "../LUFADescriptors.h"

#define INIT_DELAY		(2000 / SYSTEM_TICK_MS)


USB_ClassInfo_CDC_Device_t TerminalHandle = {
    .Config = {
        .ControlInterfaceNumber = 0,
        .DataINEndpoint = {
            .Address = CDC_TX_EPADDR,
            .Size = CDC_TXRX_EPSIZE,
            .Banks = 1,
        }, .DataOUTEndpoint = {
            .Address = CDC_RX_EPADDR,
            .Size = CDC_TXRX_EPSIZE,
            .Banks = 1,
        }, .NotificationEndpoint = {
            .Address = CDC_NOTIFICATION_EPADDR,
            .Size = CDC_NOTIFICATION_EPSIZE,
            .Banks = 1,
        },
    }
};

#ifdef CHAMELEON_TINY_UART_MODE
uint8_t bUSBTerminal = 0;
#endif
uint8_t TerminalBuffer[TERMINAL_BUFFER_SIZE] = { 0x00 };
TerminalStateEnum TerminalState = TERMINAL_UNINITIALIZED;
static uint8_t TerminalInitDelay = INIT_DELAY;

#ifndef CHAMELEON_TINY_UART_MODE
void TerminalSendString(const char *s) {
    CDC_Device_SendString(&TerminalHandle, s);
}
#else
void TerminalSendString(const char *s) {
    if (bUSBTerminal) {
        CDC_Device_SendString(&TerminalHandle, s);
    } else {
        TerminalSendBlock(s, strlen(s));
    }
}
#endif

void TerminalSendStringP(const char *s) {
    char c;

    while ((c = pgm_read_byte(s++)) != '\0') {
        TerminalSendChar(c);
    }
}

/*
void TerminalSendHex(void* Buffer, uint16_t ByteCount)
{
    char* pTerminalBuffer = (char*) TerminalBuffer;

    BufferToHexString(pTerminalBuffer, sizeof(TerminalBuffer), Buffer, ByteCount);

    TerminalSendString(pTerminalBuffer);
}

*/

#ifndef CHAMELEON_TINY_UART_MODE
void TerminalSendBlock(const void *Buffer, uint16_t ByteCount) {
    CDC_Device_SendData(&TerminalHandle, Buffer, ByteCount);
}
#else
void TerminalSendBlock(const void *Buffer, uint16_t ByteCount) {
    if (bUSBTerminal) {
        CDC_Device_SendData(&TerminalHandle, Buffer, ByteCount);
    } else {
        UartFIFOPut((uint8_t *)Buffer, ByteCount);
    }
}

void TerminalSendByte(uint8_t Byte) {
    if (bUSBTerminal) {
        CDC_Device_SendByte(&TerminalHandle, Byte);
    } else {
        UartFIFOPut(&Byte, 1);
    }
}
#endif


static void ProcessByte(void) {
    int16_t Byte = CDC_Device_ReceiveByte(&TerminalHandle);

    if (Byte >= 0) {
#ifdef CHAMELEON_TINY_UART_MODE
        bUSBTerminal = 1;
#endif
        /* Byte received */
        LEDHook(LED_TERMINAL_RXTX, LED_PULSE);

        if (XModemProcessByte(Byte)) {
            /* XModem handled the byte */
        } else if (CommandLineProcessByte(Byte)) {
            /* CommandLine handled the byte */
        }
    }
}

static void SenseVBus(void) {
    switch (TerminalState) {
        case TERMINAL_UNINITIALIZED:
            if (TERMINAL_VBUS_PORT.IN & TERMINAL_VBUS_MASK) {
                /* Not initialized and VBUS sense high */
                TerminalInitDelay = INIT_DELAY;
                TerminalState = TERMINAL_INITIALIZING;
            }
            break;

        case TERMINAL_INITIALIZING:
            if (--TerminalInitDelay == 0) {
                SystemStartUSBClock();
                USB_Init();
                TerminalState = TERMINAL_INITIALIZED;
            }
            break;

        case TERMINAL_INITIALIZED:
            if (!(TERMINAL_VBUS_PORT.IN & TERMINAL_VBUS_MASK)) {
                /* Initialized and VBUS sense low */
                TerminalInitDelay = INIT_DELAY;
                TerminalState = TERMINAL_UNITIALIZING;
#ifdef CHAMELEON_TINY_UART_MODE
                bUSBTerminal = 1;
#endif
            }
            break;

        case TERMINAL_UNITIALIZING:
            if (--TerminalInitDelay == 0) {
                USB_Disable();
                SystemStopUSBClock();
                TerminalState = TERMINAL_UNINITIALIZED;
            }
            break;

        default:
            break;
    }
}

void TerminalInit(void) {
    TERMINAL_VBUS_PORT.DIRCLR = TERMINAL_VBUS_MASK;
}

void TerminalTask(void) {
    if (TerminalState == TERMINAL_INITIALIZED) {
        CDC_Device_USBTask(&TerminalHandle);
        USB_USBTask();

        ProcessByte();
    }
}

void TerminalTick(void) {
    SenseVBus();

    if (TerminalState == TERMINAL_INITIALIZED) {
        XModemTick();
        CommandLineTick();
    }
}

/** Event handler for the library USB Connection event. */
void EVENT_USB_Device_Connect(void) {
    LEDHook(LED_TERMINAL_CONN, LED_ON);
}

/** Event handler for the library USB Disconnection event. */
void EVENT_USB_Device_Disconnect(void) {
    LEDHook(LED_TERMINAL_CONN, LED_OFF);
}


/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void) {
    CDC_Device_ConfigureEndpoints(&TerminalHandle);
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void) {
    CDC_Device_ProcessControlRequest(&TerminalHandle);
}


