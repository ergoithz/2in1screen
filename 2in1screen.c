/*
 * Screen rotation daemon (preconfigured for Lenovo Thinkpad Yoga 11e)
 *
 * Compilation: gcc -O2 -o 2in1screen 2in1screen.c
 *
 * You will probably have to tune this stuff for your case:
 *   - TRANSFORM_PROPERTIES and TRANSFORM_PROPERTIES_SIZE
 *   - TRANSFORM matrixes
 *   - function rotation_changed's conditions
 */

/*
 * TRANSFORM_PROPERTIES_SIZE: number of TRANSFORM_PROPERTIES items
 * TRANSFORM_PROPERTIES: alternating device and property items
 * To get transform devices:
 *   xinput list| grep -i 'touchpad\|pen\|finger'
 * To get transform matrix properties:
 *   xinput list-props "Wacom HID 5073 Pen" | grep "Coordinate Transformation Matrix"
 */
#define TRANSFORM_PROPERTIES_SIZE 0
char
  *TRANSFORM_PROPERTIES[TRANSFORM_PROPERTIES_SIZE] = {};

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/*
 * Tests (xyz for scale 0.019122):
 *   - Normal: 482 -20 138
 *   - Right: 26 506 -28
 *   - Left: -2 -500 -88
 *   - Inverted: -505 15 32
 *   - Horizontal (facing up): 89 -13 496
 *   - Horizontal (facing down): -95 -37 -541
 */

double
  accel_threshold = 5.0,
  accel_scale = 1,
  accel_x = 0.0,
  accel_y = 0.0,
  accel_z = 0.0;

char
  verbosity = 0,
  current_state = 0;

#define DATA_SIZE 256
char
  basedir[DATA_SIZE],
  *basedir_end = NULL,
  content[DATA_SIZE],
  *ROTATION[4] = {
    "normal",
    "inverted",
    "left",
    "right"
    },
  /*
   * Input transformation matrix:
   *   - c0: touch_area_width / total_width
   *   - c2: touch_area_height / total_height
   *   - c1: touch_area_x_offset / total_width
   *   - c3: touch_area_y_offset / total_height
   *   - matrix: c0 0 c1 0 c2 c3 0 0 1
   */
  *TRANSFORM[4] = {
    " 1  0  0  0  1 0 0 0 1",
    " 1  0  0  0  1 0 0 0 1",
    " 1  0  0  0  1 0 0 0 1",
    " 1  0  0  0  1 0 0 0 1",
    };

char rotation_changed() {

  // do not attempt to change orientation when horizontal
  if (accel_z < -accel_threshold || accel_z > accel_threshold) return 0;

  char state = 0;
  if (accel_x > accel_threshold) state = 0;
  else if (accel_x < -accel_threshold) state = 1;
  else if (accel_y > accel_threshold) state = 2;
  else if (accel_y < -accel_threshold) state = 3;

  if (current_state == state) return 0;

  current_state = state;
  return 1;
}

FILE* bdopen (char const *fname, char leave_open) {
  *basedir_end = '/';
  strcpy(basedir_end + 1, fname);
  FILE *fin = fopen(basedir, "r");
  setvbuf(fin, NULL, _IONBF, 0);
  fgets(content, DATA_SIZE, fin);
  *basedir_end = '\0';
  if (leave_open == 0) {
    fclose(fin);
    return NULL;
  }
  return fin;
}

double read_dev_accel (FILE* fd) {
  fseek(fd, 0, SEEK_SET);
  fgets(content, DATA_SIZE, fd);
  return atof(content) * accel_scale;
}

void execute(char const *command) {
  if (verbosity) {
    printf("%s\n", command);
  }
  system(command);
}

void rotate_screen() {
  char command[DATA_SIZE * 4];

  sprintf(command, "xrandr -o %s", ROTATION[current_state]);
  execute(command);

  for (char i = 0; i < TRANSFORM_PROPERTIES_SIZE; i += 2) {
    sprintf(
      command,
      "xinput set-prop \"%s\" --type=float \"%s\" %s",
      TRANSFORM_PROPERTIES[i],
      TRANSFORM_PROPERTIES[i + 1],
      TRANSFORM[current_state]
    );
    execute(command);
  }
}

void help(char const *argv0) {
  fprintf(stderr,
    "Usage: %s [OPTION]...\n"
    "Rotates screen based on accelerometer (daemon)\n"
    "\n"
    "Options:\n"
    "    --verbose    Log debug messages\n"
    "    -h, --help   Show this help\n",
    argv0
  );
}

int main(int argc, char const *argv[]) {
  FILE *pf = popen("ls /sys/bus/iio/devices/iio:device*/in_accel*", "r");
  if (!pf) {
    fprintf(stderr, "IO Error.\n");
    return 2;
  }

  char const *arg;
  for (int i = 1; i < argc; i++) {
    arg = argv[i];

    if (strcmp(arg, "--verbose") == 0) {
    verbosity = 1;
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "--help") == 0) {
    help(argv[0]);
    return 0;
    } else {
    fprintf(stderr, "Invalid argument %s\n\n", arg);
    help(argv[0]);
    return 1;
    }
  }

  if (fgets(basedir, DATA_SIZE , pf) != NULL){
    basedir_end = strrchr(basedir, '/');
    if (basedir_end) *basedir_end = '\0';
    fprintf(stderr, "Accelerometer: %s\n", basedir);
  } else {
    fprintf(stderr, "Unable to find any accelerometer.\n");
    return 1;
  }
  pclose(pf);

  bdopen("in_accel_scale", 0);
  accel_scale = atof(content);
  FILE *dev_accel_x = bdopen("in_accel_x_raw", 1);
  accel_x = atof(content) * accel_scale;
  FILE *dev_accel_y = bdopen("in_accel_y_raw", 1);
  accel_y = atof(content) * accel_scale;
  FILE *dev_accel_z = bdopen("in_accel_z_raw", 1);
  accel_z = atof(content) * accel_scale;

  while (1) {
    if (verbosity) {
      printf(
        "Accelerometer x=%2f y=%2f z=%2f (%s)\n",
        accel_x,
        accel_y,
        accel_z,
        ROTATION[current_state]
        );
    }
    if (rotation_changed()) {
      rotate_screen();
    }
    sleep(2);

    accel_x = read_dev_accel(dev_accel_x);
    accel_y = read_dev_accel(dev_accel_y);
    accel_z = read_dev_accel(dev_accel_z);
  }
  return 0;
}
