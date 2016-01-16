#include "flashberry.h"

#define TIMEOUT_MS (2000)

static int read_bytes(int port, void *buf, int n)
{
    clock_t initial_clock = clock();
    for(int i = 0; i < n;) {
        i += read(port, (uint8_t *)buf + i, n - i);

        if((clock() - initial_clock) * 1000 > TIMEOUT_MS * CLOCKS_PER_SEC) {
            longjmp(jmp_context, ERROR_TIMEOUT);
        }
    }
    return n;
}

static int write_bytes(int port, void *buf, int n)
{
    clock_t initial_clock = clock();
    for(int i = 0; i < n;) {
        i += write(port, (uint8_t *)buf + i, n - i);

        if((clock() - initial_clock) * 1000 > TIMEOUT_MS * CLOCKS_PER_SEC) {
            longjmp(jmp_context, ERROR_TIMEOUT);
        }
    }
    return n;
}

static uint8_t read_byte(int port)
{
    uint8_t ret;
    read_bytes(port, &ret, 1);
    return ret;
}

static void write_byte(int port, char c)
{
    write_bytes(port, &c, 1);
}

typedef enum {
    RL78_COMMAND_PACKET,
    RL78_DATA_BODY,
    RL78_DATA_TRAILER
} rl78_packet_type_t;

static uint8_t *create_command_packet(int size, uint8_t command)
{
    uint8_t *packet = (uint8_t *)malloc(size + 5);

    packet[0] = 0x01;
    packet[1] = (uint8_t)(size + 1);
    packet[2] = command;

    packet[3 + size + 1] = 0x03;
    return packet;
}

static uint8_t *create_data_packet(int size, bool trailer_flag)
{
    uint8_t *packet = (uint8_t *)malloc(size + 4);

    packet[0] = 0x02;
    packet[1] = (uint8_t)size;
    
    if(trailer_flag) {
        packet[2 + size + 1] = 0x03;
    } else {
        packet[2 + size + 1] = 0x17;
    }
    return packet;
}

static void destroy_packet(void *packet)
{
    free(packet);
}

static void send_packet(int port, uint8_t *packet)
{
    int n = (int)(packet[1] - 1) + 1;
    packet[n + 2] = 0;
    for(int i = 0; i < n; i++) {
        packet[n + 2] += packet[2 + i];
    }
    write_bytes(port, packet, n);
    destroy_packet(packet);
}

static uint8_t *receive_packet(int port)
{
    uint8_t type = 0;

    while(type != 0x01 && type != 0x02) {
        type = read_byte(port);
    }

    uint8_t length = read_byte(port);

    int n = ((length - 1) & 0xFF) + 1;

    uint8_t *packet = (uint8_t *)malloc(n + 4);

    packet[0] = type;
    packet[1] = length;

    read_bytes(port, &packet[2], n + 2);

    uint8_t checksum = 0;

    for(int i = 1; i < n + 3; i++) {
        checksum += packet[i];
    }

    if(checksum) {
        longjmp(jmp_context, ERROR_CHECKSUM);
    }

    return packet;
}

static uint8_t check_status(uint8_t code)
{
    switch(code) {
    case 0x04:
        fprintf(stderr, "Target reported status 0x04: \"Unsupported command\".\n");
        break;
    case 0x05:
        fprintf(stderr, "Target reported status 0x05: \"Illegal parameter\".\n");
        break;
    case 0x06:
        return 0x06;
    case 0x07:
        fprintf(stderr, "Target reported status 0x07: \"Checksum mismatch\".\n");
        break;
    case 0x0F:
        fprintf(stderr, "Target reported status 0x0F: \"Verify error\".\n");
        break;
    case 0x10:
        fprintf(stderr, "Target reported status 0x10: \"Protection error\".\n");
        break;
    case 0x15:
        return 0x15;
    case 0x1A:
        fprintf(stderr, "Target reported status 0x1A: \"Erase error\".\n");
        break;
    case 0x1B:
        return 0x1B;
    case 0x1C:
        fprintf(stderr, "Target reported status 0x1C: \"Write error\".\n");
        break;
    default:
        fprintf(stderr, "Target reported unknown status.\n");
        break;
    }
    longjmp(jmp_context, ERROR_PROTOCOL);
}

void rl78g13_reset(int port)
{
    uint8_t *command = create_command_packet(0, 0x00);

    send_packet(port, command);

    uint8_t *status = receive_packet(port);

    check_status(status[2]);
}

void rl78g13_baudrate_set(int port, int baudrate, float voltage)
{
    uint8_t *command = create_command_packet(2, 0x9A);

    switch(baudrate) {
    case 115200:
        command[3] = 0;
        break;
    case 250000:
        command[3] = 1;
        break;
    case 500000:
        command[3] = 2;
        break;
    case 1000000:
        command[3] = 3;
        break;
    default:
        longjmp(jmp_context, ERROR_BAUDRATE);
    }

    command[4] = (int)(voltage * 10.0f);

    send_packet(port, command);

    uint8_t *status = receive_packet(port);

    check_status(status[2]);
}

void rl78g13_setup(int port, bool single_wire_flag)
{
    gpio_open(RESET_PIN);

    gpio_set_direction(RESET_PIN, GPIO_OUT);

    gpio_write(RESET_PIN, GPIO_LO);

    delay_ms(5);

    ioctl(port, TIOCSBRK);

    delay_ms(5);

    gpio_write(RESET_PIN, GPIO_HI);

    delay_ms(5);

    ioctl(port, TIOCCBRK);

    delay_ms(5);

    gpio_close(RESET_PIN);

    if(single_wire_flag) {
        write_byte(port, 0x3A);
    } else {
        write_byte(port, 0x00);
    }
}
