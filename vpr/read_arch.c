#include <string.h>
#include <stdio.h>
#include <math.h>
#include "util.h"
#include "pr.h"
#include "ext.h"
#include "read_arch.h"

/* This source file reads in the architectural description of an FPGA. *
 * A # symbol anywhere in the input file denotes a comment to the end  *
 * of the line.  Put a \ at the end of a line if you want to continue  *
 * a command across multiple lines.   Non-comment lines are in the     *
 * format keyword value(s).  The entire file should be lower case.     *
 * The keywords and their arguments are:                               *
 *                                                                     *
 *   io_rat integer (sets the number of io pads which fit into the     *
 *                  space one CLB would use).                          *
 *   chan_width_io float   (Width of the channels between the pads and *
 *                          core relative to the widest core channel.) *
 *   chan_width_x [gaussian|uniform|pulse] peak <width> <xpeak> <dc>.  *
 *       (<> bracketed quantities needed only for pulse and gaussian.  *
 *       Width and xpeak values from 0 to 1.  Sets the distribution of *
 *       tracks for the x-directed channels.)                          *
 *       Other possibility:  delta peak xpeak dc.                      *
 *   chan_width_y [gaussian|uniform|pulse] peak <width> <xpeak> <dc>.  *
 *       (Sets the distribution of tracks for the y-directed channels.)*
 *   outpin class: integer [top|bottom|left|right] [top|bottom|left|   *
 *       right] ...                                                    *
 *       (Sets the class to which each pin belongs and the side(s) of  *
 *       CLBs on which the physical output pin connection(s) is (are). *
 *       All pins with the same class number are logically equivalent  *
 *       -- such as all the inputs of a LUT.  Class numbers must start *
 *       at zero and be consecutive.)                                  *
 *   inpin class: integer [top|bottom|left|right] [top|bottom|left|
 *       right] ...                                                    *
 *       (All parameters have the same meanings as their counterparts  *
 *       in the outpin statement)                                      *
 *                                                                     *
 *   NOTE:  The order in which your inpin and outpin statements appear *
 *      must be the same as the order in which your netlist (.net)     *
 *      file lists the connections to the clbs.  For example, if the   *
 *      first pin on each clb in the netlist file is the clock pin,    *
 *      your first pin statement in the architecture file must be      *
 *      an inpin statement defining the clock pin.                     *
 *                                                                     *
 *   subblocks_per_cluster <int>  (Number of LUT + ff logic blocks in  *
 *      each clb, at most).                                            *
 *   subblock_lut_size <int>  (Number of inputs to each LUT in the     *
 *      clbs.  Each subblock has subblock_lut_size inputs, one output  *
 *      and a clock input.)                                            *
 *                                                                     *
 *  The following three parameters only need to be in the architecture *
 *  file if detailed routing is going to be performed (i.e. route_type *
 *  == DETAILED).                                                      *
 *                                                                     *
 *   Fc_type [absolute|fractional]  (Are the 3 Fc values absolute      *
 *      numbers of tracks to connect to, or the fraction of the W      *
 *      tracks to which each pin can connect?)                         *
 *   Fc_output float (Sets the value of Fc -- the number of tracks     *
 *      each pin can connect to in each channel bordering the pin --   *
 *      for output pins.  The Fc_output value used is always           *
 *      min(W,Fc_selected), so set Fc to be huge if you want Fc = W.)  *
 *   Fc_input float (Sets the value of Fc for input pins.)             *
 *   Fc_pad float (Sets the value of Fc for pads.)                     *
 *   switch_block_type [subset|wilton|universal] (Chooses the type of  *
 *      switch block used.  See pr.h for details.)                     */

#define NUMINP 13   /* Number of parameters to set in arch file. */
#define NUM_DETAILED 5  /* Number needed only if detailed routing used. */

static int isread[NUMINP];
static char *names[NUMINP] = {"io_rat", "chan_width_x", "chan_width_y", 
   "chan_width_io", "outpin", "inpin", "subblocks_per_cluster", 
   "subblock_lut_size", "Fc_output", "Fc_input", "Fc_pad", "Fc_type", 
   "switch_block_type"};

static float get_float (char *ptr, int inp_num, float llim, float ulim,
            FILE *fp_arch, char *buf); 
static void check_arch(char *arch_file, enum e_route_type route_type,
            struct s_det_routing_arch det_routing_arch);
static int get_int (char *ptr, int inp_num, FILE *fp_arch, char *buf);
static void get_chan (char *ptr, struct s_chan *chan, int inp_num, 
            FILE *fp_arch, char *buf); 
static void get_pin (char *ptr, int pinnum, int type, FILE *fp_arch,
            char *buf);
static enum e_Fc_type get_Fc_type (char *ptr, FILE *fp_arch, char *buf); 
static enum e_switch_block_type get_switch_block_type (char *ptr, FILE 
            *fp_arch, char *buf); 
static void countpass (FILE *fp_arch);
static int get_class (FILE *fp_arch, char *buf);
static void fill_arch (void);


void read_arch (char *arch_file, enum e_route_type route_type,
       struct s_det_routing_arch *det_routing_arch) {

/* Reads in the architecture description file for the FPGA. */

 int i, j, pinnum;
 char *ptr, buf[BUFSIZE];
 FILE *fp_arch;

 fp_arch = my_fopen (arch_file, "r", 0);
 countpass (fp_arch);

 rewind (fp_arch);
 linenum = 0;
 pinnum = 0;

 for (i=0;i<NUMINP;i++) 
    isread[i] = 0;

 pinloc = (int **) alloc_matrix (0, 3, 0, pins_per_clb-1, sizeof (int));

 for (i=0;i<=3;i++) 
    for (j=0;j<pins_per_clb;j++) 
       pinloc[i][j] = 0; 

 while ((ptr = my_fgets(buf, BUFSIZE, fp_arch)) != NULL) { 
    ptr = my_strtok(ptr, TOKENS, fp_arch, buf);
    if (ptr == NULL) continue;                   /* Empty or comment line */

    if (strcmp(ptr,names[0]) == 0) {  /* io_rat */
       io_rat = get_int (ptr, 0, fp_arch, buf);
       continue;
    }
    if (strcmp(ptr,names[1]) == 0) { /*chan_width_x */
       get_chan(ptr, &chan_x_dist, 1, fp_arch, buf);
       continue;
    }
    if (strcmp(ptr,names[2]) == 0) { /* chan_width_y */
       get_chan(ptr, &chan_y_dist, 2, fp_arch, buf);
       continue;
    }
    if (strcmp(ptr,names[3]) == 0) { /* chan_width_io */
       chan_width_io = get_float (ptr, 3, 0. ,5000., fp_arch, buf);
       isread[3]++;
       continue;
    }
    if (strcmp(ptr,names[4]) == 0) { /* outpin */
       get_pin (ptr, pinnum, DRIVER, fp_arch, buf);
       pinnum++;
       isread[4]++;
       continue;   
    }
    if (strcmp(ptr,names[5]) == 0) { /* inpin */
       get_pin (ptr, pinnum, RECEIVER, fp_arch, buf);
       pinnum++;
       isread[5]++;
       continue;
    }
    if (strcmp(ptr,names[6]) == 0) {  /* subblocks_per_cluster */
       max_subblocks_per_block = get_int (ptr, 6, fp_arch, buf);
       continue;
    }
    if (strcmp(ptr,names[7]) == 0) {  /* subblock_lut_size */
       subblock_lut_size = get_int (ptr, 7, fp_arch, buf);
       continue;
    }
    if (strcmp(ptr,names[8]) == 0) {  /* Fc_output */
       det_routing_arch->Fc_output = get_float (ptr, 8, 0., 1.e20, fp_arch,
                       buf);
       isread[8]++;
       continue;
    }
    if (strcmp(ptr,names[9]) == 0) {  /* Fc_input */
       det_routing_arch->Fc_input = get_float (ptr, 9, 0., 1.e20, fp_arch,
                       buf);
       isread[9]++;
       continue;
    }
    if (strcmp(ptr,names[10]) == 0) {  /* Fc_pad */
       det_routing_arch->Fc_pad = get_float (ptr, 10, 0., 1.e20, fp_arch, buf);
       isread[10]++;
       continue;
    }
    if (strcmp(ptr,names[11]) == 0) {   /* Fc_type */
       det_routing_arch->Fc_type = get_Fc_type (ptr, fp_arch, buf);
       isread[11]++;
       continue;
    }
    if (strcmp(ptr,names[12]) == 0) {  /* switch_block_type */
       det_routing_arch->switch_block_type =  get_switch_block_type (ptr, 
            fp_arch, buf);
       isread[12]++;
       continue;
    }
 }
 check_arch(arch_file, route_type, *det_routing_arch);
 fclose (fp_arch);
}


static void countpass (FILE *fp_arch) {

/* This routine parses the input architecture file in order to count *
 * the number of pinclasses so storage can be allocated for them     *
 * before the second (loading) pass begins.                          */

 char buf[BUFSIZE], *ptr;
 int *pins_per_class, class, i;

 linenum = 0;
 num_class = 1;   /* Must be at least 1 class  */
 
 pins_per_class = (int *) my_calloc (num_class, sizeof (int));

 while ((ptr = my_fgets(buf, BUFSIZE, fp_arch)) != NULL) { 
    ptr = my_strtok (ptr, TOKENS, fp_arch, buf);
    if (ptr == NULL) continue;

    if (strcmp (ptr, "inpin") == 0 || strcmp (ptr, "outpin") == 0) {
       class = get_class (fp_arch, buf);

       if (class >= num_class) {
          pins_per_class = (int *) my_realloc (pins_per_class, 
             (class + 1) * sizeof (int));

          for (i=num_class;i<=class;i++) 
             pins_per_class[i] = 0;

          num_class = class + 1;
       }

       pins_per_class[class]++;
    }

/* Go to end of line (possibly continued) */

    ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
    while (ptr != NULL) {
       ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
    } 
 }

/* Check for missing classes. */

 for (i=0;i<num_class;i++) {
    if (pins_per_class[i] == 0) {
       printf("\nError:  class index %d not used in architecture "
               "file.\n", i);
       printf("          Specified class indices are not consecutive.\n");
       exit (1);
    }
 }

/* I've now got a count of how many classes there are and how many    *
 * pins belong to each class.  Allocate the proper memory.            */

 class_inf = (struct s_class *) my_malloc (num_class * sizeof (struct s_class));

 pins_per_clb = 0;
 for (i=0;i<num_class;i++) {
    class_inf[i].type = OPEN;                   /* Flag for not set yet. */
    class_inf[i].num_pins = 0; 
    class_inf[i].pinlist = (int *) my_malloc (pins_per_class[i] *
        sizeof (int));
    pins_per_clb += pins_per_class[i];
 }

 free (pins_per_class);

 clb_pin_class = (int *) my_malloc (pins_per_clb * sizeof (int));
}


static int get_class (FILE *fp_arch, char *buf) {

/* This routine is called when strtok has moved the pointer to just before *
 * the class: keyword.  It advances the pointer to after the class         *
 * descriptor and returns the class number.                                */

 int class;
 char *ptr;

 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);

 if (ptr == NULL) {
    printf("Error in get_class on line %d of architecture file.\n",linenum);
    printf("Expected class: keyword.\n");
    exit (1);
 }

 if (strcmp (ptr, "class:") != 0) {
    printf("Error in get_class on line %d of architecture file.\n",linenum);
    printf("Expected class: keyword.\n");
    exit (1);
 }

/* Now get class number. */

 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
 if (ptr == NULL) {
    printf("Error in get_class on line %d of architecture file.\n",linenum);
    printf("Expected class number.\n");
    exit (1);
 }

 class = atoi (ptr);
 if (class < 0) {
    printf("Error in get_class on line %d of architecture file.\n",linenum);
    printf("Expected class number >= 0, got %d.\n",class);
    exit (1);
 }

 return (class);
}


static void get_pin (char *ptr, int pinnum, int type, FILE *fp_arch, 
                char * buf) {

/* This routine parses an ipin or outpin line.  It should be called right *
 * after the inpin or outpin keyword has been parsed.                     */

 int i, valid, class, ipin;
 char *position[4] = {"top", "bottom", "left", "right"};

 class = get_class (fp_arch, buf);

 if (class_inf[class].type == OPEN) {  /* First time through this class. */
    class_inf[class].type = type;
 }
 else {
    if (class_inf[class].type != type) {
       printf("Error in get_pin: architecture file, line %d.\n",
          linenum);
       printf("Class %d contains both input and output pins.\n",class);
       exit (1);
    }
 }

 ipin = class_inf[class].num_pins;
 class_inf[class].pinlist[ipin] = pinnum;
 class_inf[class].num_pins++;

 clb_pin_class[pinnum] = class;
  
 ptr = my_strtok(NULL,TOKENS,fp_arch,buf);
 if (ptr == NULL) {
    printf("Error:  pin statement specifies no locations, line %d.\n",
       linenum);
    exit(1);
 }

 do {
    valid = 0;
    for (i=0;i<=3;i++) {
       if (strcmp(ptr,position[i]) == 0) {
          pinloc[i][pinnum] = 1;
          valid = 1;
          break;
       }
    }
    if (valid != 1) {
       printf("Error:  bad pin location on line %d.\n", linenum);
       exit(1);
    }
 } while((ptr = my_strtok(NULL,TOKENS,fp_arch,buf)) != NULL);
}


static enum e_Fc_type get_Fc_type (char *ptr, FILE *fp_arch, char *buf) {

/* Sets the Fc_type to either ABSOLUTE or FRACTIONAL.                    */

 enum e_Fc_type Fc_type;

 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
 if (ptr == NULL) {
    printf("Error:  missing Fc_type value on line %d of "
         "architecture file.\n", linenum);
    exit (1);
 }
 
 if (strcmp (ptr, "absolute") == 0) {
    Fc_type = ABSOLUTE;
 }
 else if (strcmp (ptr, "fractional") == 0) {
    Fc_type = FRACTIONAL;
 }
 else {
    printf("Error:  Bad Fc_type value (%s) on line %d of "
         "architecture file.\n", ptr, linenum);
    exit (1);
 }
 
 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
 if (ptr != NULL) {
    printf("Error:  extra characters at end of line %d.\n", linenum);
    exit (1);
 }
 
 return (Fc_type);
}


static enum e_switch_block_type get_switch_block_type (char *ptr, FILE 
              *fp_arch, char *buf) {

/* Returns the proper value for the switch_block_type member of        *
 *  det_routing_arch.                                                  */

 enum e_switch_block_type sblock_type;

 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
 if (ptr == NULL) {
    printf("Error:  missing switch_block_type value on line %d of "
         "architecture file.\n", linenum);
    exit (1);
 }

 if (strcmp (ptr, "subset") == 0) {
    sblock_type = SUBSET;
 }
 else if (strcmp (ptr, "wilton") == 0) {
    sblock_type = WILTON;
 }
 else if (strcmp (ptr, "universal") == 0) {
    sblock_type = UNIVERSAL;
 }
 else {
    printf("Error:  Bad switch_block_type value (%s) on line %d of "
         "architecture file.\n", ptr, linenum);
    exit (1);
 }

 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
 if (ptr != NULL) {
    printf("Error:  extra characters at end of line %d.\n", linenum);
    exit (1);
 }

 return (sblock_type);
}


static int get_int (char *ptr, int inp_num, FILE *fp_arch, char *buf) {

/* This routine gets the next integer on the line.  It must be greater *
 * than zero or an error message is printed.                           */

 int val;

 ptr = my_strtok(NULL,TOKENS,fp_arch,buf);
 if (ptr == NULL) {
    printf("Error:  missing %s value on line %d.\n",
       names[inp_num],linenum);
    exit(1);
 }
 val = atoi(ptr);
 if (val <= 0) {
    printf("Error:  Bad value.  %s = %d on line %d.\n",
       names[inp_num],val,linenum);
    exit(1);
 }

 ptr = my_strtok (NULL, TOKENS, fp_arch, buf);
 if (ptr != NULL) {
    printf("Error:  extra characters at end of line %d.\n", linenum);
    exit (1);
 }

 isread[inp_num]++;
 return(val);
}


static float get_float (char *ptr, int inp_num, float low_lim, 
   float upp_lim, FILE *fp_arch, char *buf) {

/* This routine gets the floating point number that is next on the line. *
 * low_lim and upp_lim specify the allowable range of numbers, while     *
 * inp_num gives the type of input line being parsed.                    */

 float val;
 
 ptr = my_strtok(NULL,TOKENS,fp_arch,buf);
 if (ptr == NULL) {
    printf("Error:  missing %s value on line %d.\n",
       names[inp_num],linenum);
    exit(1);
 }

 val = atof(ptr);
 if (val <= low_lim || val > upp_lim) {
    printf("Error:  Bad value parsing %s. %g on line %d.\n",
       names[inp_num],val,linenum);
    exit(1);
 }

 return(val);
}


/* Order:  chan_width_x [gaussian|uniform|pulse] peak <width>  *
 * <xpeak> <dc>.  (Bracketed quantities needed only for pulse  *
 * and gaussian).  All values from 0 to 1, except peak and dc, *
 * which can be anything.                                      *
 * Other possibility:  chan_width_x delta peak xpeak dc        */

static void get_chan (char *ptr, struct s_chan *chan, int inp_num, 
   FILE *fp_arch, char *buf) {

/* This routine parses a channel functional description line.  chan  *
 * is the channel data structure to be loaded, while inp_num is the  *
 * type of input line being parsed.                                  */

 ptr = my_strtok(NULL,TOKENS,fp_arch,buf);
 if (ptr == NULL) {
    printf("Error:  missing %s value on line %d.\n",
       names[inp_num],linenum);
    exit(1);
 }

 if (strcmp(ptr,"uniform") == 0) {
    isread[inp_num]++;
    chan->type = UNIFORM;
    chan->peak = get_float(ptr,inp_num,0.,1., fp_arch, buf);
    chan->dc = 0.;
 }
 else if (strcmp(ptr,"delta") == 0) {
    isread[inp_num]++;
    chan->type = DELTA;
    chan->peak = get_float(ptr,inp_num,-1.e5,1.e5, fp_arch, buf); 
    chan->xpeak = get_float(ptr,inp_num,-1e-30,1., fp_arch, buf);
    chan->dc = get_float(ptr,inp_num,-1e-30,1., fp_arch, buf);
 }
 else {
    if (strcmp(ptr,"gaussian") == 0) 
       chan->type = GAUSSIAN; 
    if (strcmp(ptr,"pulse") == 0) 
       chan->type = PULSE;
    if (chan->type == GAUSSIAN || chan->type == PULSE) {
       isread[inp_num]++;
       chan->peak = get_float(ptr,inp_num,-1.,1., fp_arch, buf); 
       chan->width = get_float(ptr,inp_num,0.,1.e10, fp_arch, buf);
       chan->xpeak = get_float(ptr,inp_num,-1e-30,1., fp_arch, buf);
       chan->dc = get_float(ptr,inp_num,-1e-30,1., fp_arch, buf);
    } 
 }

 if (isread[inp_num] == 0) {
    printf("Error:  %s distribution keyword: %s unknown.\n",
       names[inp_num],ptr);
    exit(1);
 }

 if (my_strtok(NULL,TOKENS,fp_arch,buf) != NULL) {
    printf("Error:  extra value for %s at end of line %d.\n",
       names[inp_num],linenum);
    exit(1);
 }
}
    

static void check_arch(char *arch_file, enum e_route_type route_type,
        struct s_det_routing_arch det_routing_arch) {

/* This routine checks that the input architecture file makes sense and *
 * specifies all the needed parameters.  The parameters must also be    *
 * self-consistent and make sense.                                      */

 int i, fatal, num_to_check;

 fatal = 0;

/* NUMINP parameters are expected in the architecture file.  The first *
 * NUMINP - NUM_DETAILED are always mandatory.  The last NUM_DETAILED  *
 * ones are needed only if detailed routing is going to be performed.  *
 * Expect the corresponding isread to be 1, except isread[4] (outpin)  *
 * and isread[5] (inpin)  which should both be greater than 0.         */

 if (route_type == DETAILED) 
    num_to_check = NUMINP;
 else
    num_to_check = NUMINP - NUM_DETAILED;
 
 for (i=0;i<num_to_check;i++) {
    if (i != 4 && i != 5) {
       if (isread[i] == 0) {
          printf("Error:  %s not set in file %s.\n",names[i],
             arch_file);
          fatal=1;
       }
       if (isread[i] > 1) {
          printf("Error:  %s set %d times in file %s.\n",names[i],
              isread[i],arch_file);
          fatal = 1;
       }
    }

    else {    /* outpin or inpin lines */
       if (isread[i] < 1) {
          printf("Error:  in file %s.  Clb has %d %s(s).\n",arch_file, 
                  isread[i], names[i]);
          fatal = 1;
       }
    }
 }

 if (fatal) exit(1);

/* Detailed routing is only supported on architectures with all channel  *
 * widths the same for now.  The router could handle non-uniform widths, *
 * but the routing resource graph generator doesn't build the rr_graph   *
 * for the nonuniform case as yet.                                       */

 if (route_type == DETAILED) {
    if (chan_x_dist.type != UNIFORM || chan_y_dist.type != UNIFORM || 
         chan_x_dist.peak != chan_y_dist.peak || chan_x_dist.peak != 
         chan_width_io) {
       printf("Error in check_arch:  detailed routing currently only\n"
             "supported on FPGAs with all channels of equal width.\n");
       exit (1);
    }

    if (det_routing_arch.Fc_type == ABSOLUTE) {
       if (det_routing_arch.Fc_output < 1 || det_routing_arch.Fc_input < 1
              || det_routing_arch.Fc_pad < 1) {
          printf ("Error in check_arch:  Fc values must be >= 1 in absolute "
                  "mode.\n");
          exit (1);
       }
    }
    else {   /* FRACTIONAL mode */
       if (det_routing_arch.Fc_output > 1. || det_routing_arch.Fc_input > 1.
              || det_routing_arch.Fc_pad > 1.) {
          printf ("Error in check_arch:  Fc values must be <= 1. in "
                 "fractional mode.\n");
          exit (1);
       }
    }
 }     /* End if route_type == DETAILED */
}

       
void print_arch (char *arch_file, enum e_route_type route_type,
       struct s_det_routing_arch det_routing_arch) {

/* Prints out the architectural parameters for verification in the  *
 * file "arch.echo."  The name of the architecture file is passed   *
 * in and is printed out as well.                                   */

 int i, j;
 FILE *fp;

 fp = my_fopen ("arch.echo", "w", 0);

 fprintf(fp,"Input netlist file: %s\n\n",arch_file);

 fprintf(fp,"io_rat: %d.\n",io_rat);
 fprintf(fp,"chan_width_io: %g  pins_per_clb (pins per clb): %d\n",
      chan_width_io, pins_per_clb);

 fprintf(fp,"\n\nChannel Types:  UNIFORM = %d; GAUSSIAN = %d; PULSE = %d;"
    " DELTA = %d\n\n", UNIFORM, GAUSSIAN, PULSE, DELTA);

 fprintf(fp,"\nchan_width_x:\n");
 fprintf(fp,"type: %d  peak: %g  width: %g  xpeak: %g  dc: %g\n",
   chan_x_dist.type, chan_x_dist.peak, chan_x_dist.width, 
     chan_x_dist.xpeak, chan_x_dist.dc);

 fprintf(fp,"\nchan_width_y:\n");
 fprintf(fp,"type: %d  peak: %g  width: %g  xpeak: %g  dc: %g\n\n",
   chan_y_dist.type, chan_y_dist.peak, chan_y_dist.width, 
     chan_y_dist.xpeak, chan_y_dist.dc);

 fprintf(fp,"Pin #\tclass\ttop\tbottom\tleft\tright");
 for (i=0;i<pins_per_clb;i++) {
    fprintf(fp,"\n%d\t%d\t", i, clb_pin_class[i]);
    for (j=0;j<=3;j++) 
       fprintf(fp,"%d\t",pinloc[j][i]);
 }

 fprintf(fp,"\n\nClass types:  DRIVER = %d; RECEIVER = %d\n\n", DRIVER, 
    RECEIVER);

 fprintf(fp,"Class\tType\tNumpins\tPins");
 for (i=0;i<num_class;i++) {
    fprintf(fp,"\n%d\t%d\t%d\t", i, class_inf[i].type, class_inf[i].num_pins);
    for (j=0;j<class_inf[i].num_pins;j++) 
       fprintf(fp,"%d\t",class_inf[i].pinlist[j]);
 }
 fprintf(fp,"\n\n");

 fprintf(fp,"subblocks_per_cluster (maximum): %d\n",
       max_subblocks_per_block);

 fprintf(fp,"subblock_lut_size: %d\n", subblock_lut_size);

 if (route_type == DETAILED) {
    fprintf(fp,"\n");
    if (det_routing_arch.Fc_type == ABSOLUTE) 
       fprintf(fp,"Fc value is absolute number of tracks.\n");
    else 
       fprintf(fp,"Fc value is fraction of tracks in a channel.\n");
   
    fprintf(fp,"Fc_output: %g.  Fc_input: %g.  Fc_pad: %g.\n", 
            det_routing_arch.Fc_output, det_routing_arch.Fc_input, 
            det_routing_arch.Fc_pad);

    if (det_routing_arch.switch_block_type == SUBSET) 
       fprintf (fp, "switch_block_type: SUBSET.\n");
    else if (det_routing_arch.switch_block_type == WILTON)
       fprintf (fp, "switch_block_type: WILTON.\n");
    else
       fprintf (fp, "switch_block_type: UNIVERSAL.\n");
 }

 fclose (fp);
}


void init_arch (float aspect_ratio, boolean user_sized) {

/* Allocates various data structures that depend on the FPGA         *
 * architecture.  Aspect_ratio specifies how many columns there are  *
 * relative to the number of rows -- i.e. width/height.  Used-sized  *
 * is TRUE if the user specified nx and ny already; in that case     *
 * use the user's values and don't recompute them.                   */

 int io_lim;


/* User specified the dimensions on the command line.  Check if they *
 * will fit the circuit.                                             */

 if (user_sized == TRUE) {
    if (num_clbs > nx * ny || num_p_inputs + num_p_outputs > 
           2 * io_rat * (nx + ny)) {
       printf ("Error:  User-specified size is too small for circuit.\n"); 
       exit (1);
    }
 }

/* Size the FPGA automatically to be smallest that will fit circuit */

 else {
/* Area = nx * ny = ny * ny * aspect_ratio                  *
 * Perimeter = 2 * (nx + ny) = 2 * ny * (1. + aspect_ratio)  */

    ny = (int) ceil (sqrt ((double) (num_clbs / aspect_ratio)));
    io_lim = (int) ceil ((num_p_inputs + num_p_outputs) / (2 * io_rat *
           (1. + aspect_ratio)));
    ny = max (ny, io_lim);

    nx = (int) ceil (ny * aspect_ratio);
 }

/* If both nx and ny are 1, we only have one valid location for a clb. *
 * That's a major problem, as won't be able to move the clb and the    *
 * find_to routine that tries moves in the placer will go into an      *
 * infinite loop trying to move it.  Exit with an error message        *
 * instead.                                                            */

 if (nx == 1  && ny == 1 && num_clbs != 0) {
    printf ("Error:\n");
    printf ("Sorry, can't place a circuit with only one valid location\n");
    printf ("for a logic block (clb).\n");
    printf ("Try me with a more realistic circuit!\n");
    exit (1);
 }

/* To remove this limitation, change ylow etc. in struct s_rr_node to *
 * be ints instead.  Used shorts to save memory.                      */

 if (nx > 32766 || ny > 32766) {
    printf("Error:  nx and ny must be less than 32767, since the \n");
    printf("router uses shorts (16-bit) to store coordinates.\n");
    printf("nx: %d.  ny: %d.\n", nx, ny);
    exit (1);
 }

 clb = (struct s_clb **) alloc_matrix (0, nx+1, 0, ny+1, 
              sizeof(struct s_clb));

 chan_width_x = (int *) my_malloc ((ny+1) * sizeof(int));
 chan_width_y = (int *) my_malloc ((nx+1) * sizeof(int)); 

 fill_arch();
}


static void fill_arch (void) {

/* Fill some of the FPGA architecture data structures.         */

 int i, j, *index;

/* allocate io_blocks arrays. Done this way to save storage */

 i = 2*io_rat*(nx+ny);  
 index = (int *) my_malloc (i*sizeof(int));
 for (i=1;i<=nx;i++) {
    clb[i][0].u.io_blocks = index;
    index+=io_rat;
    clb[i][ny+1].u.io_blocks = index;
    index+=io_rat;
 }
 for (i=1;i<=ny;i++) {
    clb[0][i].u.io_blocks = index;
    index+=io_rat;
    clb[nx+1][i].u.io_blocks = index;
    index+=io_rat;
 }
 
 /* Initialize type, and occupancy. */

 for (i=1;i<=nx;i++) {  
    clb[i][0].type = IO;
    clb[i][ny+1].type = IO;  /* perimeter (IO) cells */
 }

 for (i=1;i<=ny;i++) {
    clb[0][i].type = IO;
    clb[nx+1][i].type = IO;
 }

 for (i=1;i<=nx;i++) {   /* interior (LUT) cells */
    for (j=1;j<=ny;j++) {
       clb[i][j].type = CLB;
    }
 }

/* Nothing goes in the corners.      */

 clb[0][0].type = clb[nx+1][0].type = ILLEGAL;  
 clb[0][ny+1].type = clb[nx+1][ny+1].type = ILLEGAL;
}
