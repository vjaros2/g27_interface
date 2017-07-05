#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static const char *bus_name(const int bustype)
{
    static char buffer[16];
    switch (bustype) {
    case 0:             return "None, or built-in (0)";
    case BUS_PCI:       return "PCI (BUS_PCI)";
    case BUS_ISAPNP:    return "ISA PnP (BUS_ISAPNP)";
    case BUS_USB:       return "USB (BUS_USB)";
    case BUS_HIL:       return "HP-HIL (BUS_HIL)";
    case BUS_BLUETOOTH: return "Bluetooth (BUS_BLUETOOTH)";
    case BUS_VIRTUAL:   return "Virtual (BUS_VIRTUAL)";
    case BUS_ISA:       return "ISA (BUS_ISA)";
    case BUS_I8042:     return "i8042 (BUS_I8042)";
    case BUS_XTKBD:     return "XT Keyboard (BUS_XTKBD)";
    case BUS_RS232:     return "Serial port (BUS_RS232)";
    case BUS_GAMEPORT:  return "Gameport (BUS_GAMEPORT)";
    case BUS_PARPORT:   return "Parallel port (BUS_PARPORT)";
    case BUS_AMIGA:     return "Amiga (BUS_AMIGA)";
    case BUS_ADB:       return "ADB (BUS_ADB)";
    case BUS_I2C:       return "I2C (BUS_I2C)";
    case BUS_HOST:      return "Host (BUS_HOST)";
    case BUS_GSC:       return "GSC (BUS_GSC)";
    case BUS_ATARI:     return "Atari (BUS_ATARI)";
    case BUS_SPI:       return "SPI (BUS_SPI)";
    default:
        snprintf(buffer, sizeof buffer, "%04x", bustype & 65535);
        return (const char *)buffer;
    }
}

void print_usage(){
	printf("./a.out [event number to open] [tolerance (20-500)] [position to move to(16384 max)] [ff strength (32767 max)]\n");
}

static int test_bit(int nr, const volatile unsigned long* addr){
	return (1UL & (((const int *) addr)[nr >> 5] >> (nr & 31))) != 0UL;
}

void get_current_x(int fd, struct input_absinfo* abs_feat){
   	if(ioctl(fd, EVIOCGABS(ABS_X), abs_feat))
	    perror("evdev ioctl");
}

int set_wheel_position(int fd, size_t to, size_t tolerance, size_t speed){
	to = to & 0x3FFF; // limit to 16383 max value;
	struct input_event play, stop;
	struct ff_effect effect;
	struct input_absinfo abs_feat;
	
	memset(&abs_feat, 0x0, sizeof(abs_feat));
	get_current_x(fd, &abs_feat);
	get_current_x(fd, &abs_feat);
	size_t from = abs_feat.value;
	
	printf("current position: %zu\n", from);
	printf("rotating to position: %zu\n", to);
		
	/* Upload a constant effect (force devices) */
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_CONSTANT;
	effect.id = -1;
	effect.u.constant.level = speed & 0x7FFF;
	
	if(from+4 < to-4){ // we need to turn right
		printf("turning right\n");
		effect.direction = 0xC000;	/* 270 degrees */
	}
	else if(from-4 > to+4){ //we need to turn left
		printf("turning left\n");
		effect.direction = 0x4000;	/* 90 degrees */
	}
	else
		return abs_feat.value; // no rotation necessary.
	
	if (ioctl(fd, EVIOCSFF, &effect) < 0) {
			perror("Upload effect");
			exit(1);
	}
	effect.u.constant.level = 0x1930; // make it weaker, this is the minimum force able to maintain rotation.
	/* Play the effect */
	play.type = EV_FF;
	play.code = effect.id;
	play.value = 1;

	if (write(fd, (const void*) &play, sizeof(play)) == -1) {
		perror("Play effect");
		exit(1);
	}
   	
   	
   	//wait until we're close
   	while(abs_feat.value > (to + tolerance) || abs_feat.value < (to - tolerance)){
   		get_current_x(fd, &abs_feat);
   	}
	

	//slow down by triggering the weak force
	if (ioctl(fd, EVIOCSFF, &effect) < 0) {
			perror("Upload effect");
	}
	
	while(abs_feat.value > (to+4) || abs_feat.value < (to-4)){ //plus or minus 3 is good enough
   		get_current_x(fd, &abs_feat);
   	}

	usleep(10); //miniscule delay so the ioctl buffer is sure to be read
   	
   	/* Stop the effect */
	play.value = 0;

	if (write(fd, (const void*) &play, sizeof(play)) == -1) {
		perror("Play effect");
		exit(1);
	}
	
	/* Clear the effect */
	if (ioctl(fd, EVIOCRMFF, effect.id) < 0) {
		perror("Erase effect");
		exit(1);
	}
   	get_current_x(fd, &abs_feat);
   	return abs_feat.value;
}


//simple demo that prints information about the device and sends force feedback events.
int main(int argc, char** argv){
	if(argc != 5){
		print_usage();
		return -1;
	}
	char buf[24];
	sprintf(buf, "/dev/input/event%s", argv[1]);
	int fd = open(buf, O_RDWR);
	int tolerance = atoi(argv[2]);
	int position = atoi(argv[3]);
	int speed = atoi(argv[4]);
	
	int version = 0;
	if (ioctl(fd, EVIOCGVERSION, &version))
    	perror("evdev ioctl");
    
	printf("evdev driver version is %d.%d.%d\n",
       version >> 16, (version >> 8) & 0xff,
       version & 0xff);
    
    char name[512] = "Unknown";
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
    	perror("evdex ioctl name");
    printf("Device Name: %s\n", name);
    
    char phys[512] = "Unknown";
    if(ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) < 0)
    	perror("event ioctl path");
	printf("Device path: %s\n", phys);
    
    struct input_id device_info;
    if(ioctl(fd, EVIOCGID, &device_info))
    	perror("evdev ioctl");
	printf("Bus Type: %s\n", bus_name(device_info.bustype));
    
    char uniq[512] = "Unknown";
    if(ioctl(fd, EVIOCGUNIQ(sizeof(uniq)), uniq) < 0)
    	perror("event ioctl uniq");
	printf("Device identity: %s\n", uniq);
	
	unsigned long evtype_b = 0;
	if (ioctl(fd, EVIOCGBIT(0, EV_MAX), &evtype_b) < 0)
    	perror("evdev ioctl");
	printf("Supported event types:\n");

	for (unsigned short yalv = 0; yalv < EV_MAX; yalv++) {
		if (test_bit(yalv, &evtype_b)) {
		    /* the bit is set in the event types list */
		    printf("	Event type 0x%02x ", yalv);
		    switch ( yalv)
		        {
		        case EV_SYN :
		            printf(" (Synch Events)\n");
		            break;
		        case EV_KEY :
		            printf(" (Keys or Buttons)\n");
		            break;
		        case EV_REL :
		            printf(" (Relative Axes)\n");
		            break;
		        case EV_ABS :
		            printf(" (Absolute Axes)\n");
		            break;
		        case EV_MSC :
		            printf(" (Miscellaneous)\n");
		            break;
		        case EV_LED :
		            printf(" (LEDs)\n");
		            break;
		        case EV_SND :
		            printf(" (Sounds)\n");
		            break;
		        case EV_REP :
		            printf(" (Repeat)\n");
		            break;
		        case EV_FF :
		        case EV_FF_STATUS:
		            printf(" (Force Feedback)\n");
		            break;
		        case EV_PWR:
		            printf(" (Power Management)\n");
		            break;
		        default:
		            printf(" (Unknown: 0x%04hx)\n",
		         yalv);
		        }
		}
	}
	
	
	//SEND FORCE FEEDBACK
	
	int curr = set_wheel_position(fd, position, tolerance, speed);
	printf("wheel is at: %d\n", curr);
    close(fd);
    
    return 0;
}
