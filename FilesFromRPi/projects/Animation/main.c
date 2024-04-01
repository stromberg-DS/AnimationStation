///////////////////////////////////////////////////////////////////////
//
// Stop Motion Animation
//
// File locations:
// 
// All files are in the /home/pi/projects/Animation folder. Within this
// folder are the single source file, main.c (this file), the object and
// executables, and the makefile. The Frames folder contains the 
// captured frames of the current video being made. The frame files are 
// named Framennnnn.jpg where the "nnnnn" a 5-digit number with leading
// zeros indicating the sequence number of the frame.
//
// Button Implementation:
//
// The GPIO pins are used to read the buttons using a positive logic
// (pulling the pin high to indicate a pressed button.) Evenusing 
// internal pulldown resistors, this has given flase positives. So each
// button is a SPDT switch with the sensing line connected to the COM,
// the 3.3 volt line to the NO and a ground line to the NC to give a 
// hard ground to unpressed buttons.
//
// There are 7 buttons, defined:
// 
//
// There is an additional button for floor staff to shut down the 
// program before killing the power.
//
// GPIO Pin anomalies:
//  Pins 3 and 5 have a hardwired pull up resistor and cannot be used
//  General Purpose I/O (GPIO). Pin 13 would not respond as an input pin. 
//
// Wiring:
//  Wiring to the Raspberry Pi is done through a CZH-Labs.com ultra
//  small terminal breakout board. The bcm2835 GPIO numbering matches 
//  the numbering on the breakout board with an "IO" prefaced to the
//  GPIO number on the (e.g. GPIO pin 4 is labelled IO4)
//  GPIO connector. The label for the pins used here are:
//
//  Breakout  #define      Function
//
//   IO24       PLAY       Play saved animation
//   IO25      RECORD      Add the current image to the animation
//   IO16      SHUTDOWN    Shutdown the program before shutting off power
//   IO21      RESTART     Erase current animation
//
//  Each of these should be wired to the COM terminal on the microswitch
//  A line from a 3.3 volt pin should connect to all NO terminals
//  on the microswitches  and a ground line to all NC terminals.
//
// Pin defines for the buttons

#define PLAY      24  // Green
#define RECORD    25  // Orange
#define RESTART   12  // Erase and start over
#define SHUTDOWN  16  // Shutdown program 
#define NO_BUTTON -1

// For debugging
#define DEBUG 1      // Print debug messages
#define USE_KBD 1    // For keyboard use without buttons
#define USE_CAMERA 1 // To leave camera off for debug  

// Video Implementation:
// 
// This ended up requiring multiple camera handling packages to work
// properly.
//
// libcamera is the standard Raspberry Pi labrary for handling the
// camera. Here, this is used only to display live video. If it
// is used to grab frames it adds a minimum 1 second (5 second default)
// delay that is unacceptable here.
//
// libcamera command:
// libcamera-vid //shows real time video
// options:
// -t 0 // keep video running until cancelled
// --width nnn --height nnn // set camera resolution
// -p x,y,width,height // set location and size of preview
// -F fullscreen

// Instead, Raspberry Pi's built-in screen shot function scrot is used
// to grab frames from the video. This grabs the entire monitor 
// display but is very quick. Using scrot instead of libcamera-still 
// allows the live video to keep running.
//
// scrot command:
// scrot <output file>
//
// The feh library is used to play the animation. The one command plays
// all files in the Frames folder in numerical order
//
// The command is:
// feh --quiet --hide-pointer  -Z -p --on-last-slide=quit --slideshow-delay 0.001 --recursive 
// feh options are:
// -F --fullscreen
// -Z --auto-zoom : zoom to screen size in fullscreen
// --on-last-slide=quit run the animation then quit.
// -p --preload : check for non-loadable images first
// --slideshow-delay time between slides in seconds
// --recursive show subfolders as well. SInce only Frames is a subfolder
// --quiet to suppress output to the console
// this just shows the saved animation.
// 
// Erasing frames uses the standard linux remove command rm
//
// To hide task bar, in task bar, right clink on "Panel Settings"
// -> Advanced. Check Minimize panel when not in use
// The geometry tab allows task bar to be positioned.
//
// To set the screen resolution of the monitor to match those in 
// the program (set in the V_WIDE and V_HIGH #defines) go to 
// Preferences->Screen Configuration and right click on the HDMI-1
// area that shows up. Left click on "Resolution" option in the 
// drop down menu.
//
// This reqiures forked processes to allow the multiple video handlers 
// to be used. Forked processes are used for the live video feed
// via libcamera and to play the animation using feh. Both of these 
// are started via a system() command. When they are started. the
// getpid() command is used to get the new pid and store it in the
// shared memory Share[] array. Unfortunately, at least for feh,
// executing the system command creates a process but that then
// creates another process whose pid is not retrieveable via getpid()
// instead, I use the ps command and pipe it to a file then look for
// feh under the CMD column to find the pid to kill.
//
// the ps command (process status) to list all processes and pipe it
// to proc.txt is:
//
// ps a > proc.txt

///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <ctype.h>      // for toupper
#include <termios.h>  
#include <string.h>
#include <stdlib.h>
#include <unistd.h>     // Needeed for sleep() and usleep()
#include <bcm2835.h>
#include <sys/mman.h>
#include <sys/select.h> // Needed for kbhit()
#include <sys/ioctl.h>  // Needed for kbhit()
#include <time.h>
                  
// Basic defines
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define RANGE(a, b, c) ((a) > (b) ? (a) : ((c) < (b) ? (c) : (b)))

// Set this to match the screen resolution
#define V_WIDE 1920
#define V_HIGH 1080

#define VIDEO_PID  0
#define FRAME_PID  1
#define NO_PID    -1

#define FULL_PATH "/home/rpi/projects/Animation/"

// Globals, Assign the button defines to an array to allow button 
// checking in a loop 
int Buttons[] = { PLAY, RECORD, RESTART, SHUTDOWN };
// Name the buttons for use in diagnostic messages
char BText[][32] = { "Play", "Record", "Restart", "Shutdown"};
    
// Figure how many buttons are defined
int NumButtons = sizeof(Buttons)/sizeof(Buttons[0]);
int FrameCount;     // Keeps count of total frames recorded
int CurrentFrame;   // Track current frame location
int CurrentPreview; // Track which video is being previewed

int *Share; // Shared memory to get the camera pid. This allows the
// Continous video started in StartCamera() to be stopped by KillCamera()
// Otherwise the live video can't be stopped

int main()
{
 void StartCamera();
 void Restart();
 void InitGPIO();
 int  ReadButtons();
 void Play();
 void GrabFrame(int Frame, int Wide, int High);
 void Shutdown();
 void ShowPressedButton(int Button);
 
 int B;

 // Allocate the shared memory for the video camera pid
 Share = mmap(NULL, 32, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
 // Set the PIDs to show not forked processes
 Share[VIDEO_PID] = NO_PID;
 Share[FRAME_PID] = NO_PID;

 // Move to the program folder. This does not seem to always work since
 // full paths are often needed.
// system("cd /home/rpi/projects/Animation");
 
 Restart();        // Initialize values
 InitGPIO();       // Start the BCM2835 library to allow reading the buttons
 if(USE_CAMERA) StartCamera();    // Turn on the live video

 while(1)
 {
  if((B = ReadButtons()) != NO_BUTTON) // Look for a button press
  {
   ShowPressedButton(B);
   switch(B)
   {
    // Delete the current video   
    case RESTART   : Restart();                                   break;
    // Play the animations 
    case PLAY      : Play();                                      break;
    // Grab the current image an put it last in the sequence 
    case RECORD    : GrabFrame(FrameCount, V_WIDE, V_HIGH);       break;
    // Shutdown the program in preparation of power off.
    case SHUTDOWN  : Shutdown();                                  break;
   }
  } 
 }
 // bcm2835_delayMicroseconds(500000);
 return 1;
}

///////////////////////////////////////////////////////////////////////
//
// Button handling functions
//
///////////////////////////////////////////////////////////////////////

// Button wires are attached to the COM pin on the microswitches and
// a button press is read as a HIGH value. The pins are configured
// with pull down resistors but a ground is attached to the NC terminals.

// Initialize the pushbuttons
void InitGPIO()
{
 int i;

 // Initialize the BCM2835 library, used to read button presses here
 if(!bcm2835_init()) printf("No BCM2835\n");    

 for(i=0; i<NumButtons; i++)
 {
  // Configure all button pins as input      
  bcm2835_gpio_fsel(Buttons[i],BCM2835_GPIO_FSEL_INPT);  
  // Add pulldown resistors to all pins
  bcm2835_gpio_set_pud(Buttons[i],BCM2835_GPIO_PUD_DOWN);  
 } 
}

// A basic button read just looks for the first pressed button in the
// array. Alternatively, if USE_KBD is pressed, the number keys 
// represent the indices in Buttons starting at 1
int ReadButtons()
{
 int kbhit();

 int i, Found = -1;

 // bcm2835_gpio_lev(); returns HIGH or LOW
 if(USE_KBD && kbhit())
 {
  i = getchar() - '1';
  if(i > 0 && i < 8) return Buttons[i];
 }

 // Query each button in sequence

 // Only the first button found is returned but the all buttons are
 // scanned and displayed in the DEBUG print since there have been 
 // lots of issues with false button reads, both from the actual
 // switches and hardware and from Raspberry Pi anomalies.
 for(i=0; i<NumButtons; i++) if(bcm2835_gpio_lev(Buttons[i]) == HIGH)
 {
//  if(DEBUG) printf("Button %s Pressed\n", BText[i]);
  if(Found == -1) Found = i; // If it's the first press found, save it
 }
 // If a button has been pressed and at least 1 second has elapsed
 if(Found != -1) return Buttons[Found]; // Send the button info back
 return NO_BUTTON;
}

///////////////////////////////////////////////////////////////////////
//
// Display Functions
//
///////////////////////////////////////////////////////////////////////
/*
void ShowFrameInCurrent(int Frame)
{
 void ShowFrame(char *Frame); 
  
 extern int FrameCount, CurrentFrame; 
  
 char s[128];
 // Decrement the frame position with wrap around
 if(CurrentFrame < 0)  CurrentFrame = FrameCount-1;
 if(CurrentFrame >= FrameCount)  CurrentFrame = 0;
 // Build the frame file name
 sprintf(s, "Frames/Frame%05d.jpg", CurrentFrame);
 // Show the frame
 ShowFrame(s);
}
*/
// Kill the frame process
void KillFrame()
{
 void ps_kill(char *p);   
    
 extern int *Share; // PIDs are in shared memory     
 
 char s[64];
 // Build the kill command for the saved pid
 sprintf(s, "kill -kill %d", Share[FRAME_PID]);
 if(DEBUG) printf("Kill command: %s\n", s);
 // Kill the process
 system(s);
 // Now find the feh process and kill that
 ps_kill("feh");
}

// Playing the recorded video uses the feh routine to play all frames in
// the Frames folder.
void Play()
{
 void PlayVideo(char *Folder); 

 PlayVideo("Frames");
}

// Add the current view to the animation
void GrabFrame(int n, int w, int h)
{
 void StartCamera();
 void KillCamera();   
 void SystemFile(char *Command, char *File);

 char s[256];	

// KillCamera();	    
 // Going to full screen simplifies the above since scot can directly
 // save the image
printf("1: %s\n", s);
 sprintf(s, "scrot %sFrames/Frame%05d.jpg", FULL_PATH, n);

//sprintf(s, "scrot %sFrames/Grab.jpg", FULL_PATH);
printf("1: %s\n", s);
system(s);
//sprintf(s, "convert %sFrames/Grab.jpg -resize %dX%d  %sFrames/Frame%05d.jpg", FULL_PATH, V_WIDE/2, V_HIGH/2, FULL_PATH, n);
//system(s); 
// Halve the resolution of the grabbed image and store it 
 CurrentFrame++;  // Update the current frame index 
 FrameCount++;    // And the total frame count
 if(DEBUG) printf("Record Frame #%d\n", FrameCount);
 SystemFile("feh --quiet --hide-pointer  -F -p --on-last-slide=quit --slideshow-delay 0.3 %s%s", "BlackOut");
}

// Erase all frames and reset the counters 
void Restart()
{
 void SystemFile(char *Command, char *File);
  
 // Initialize the counters
 FrameCount = 0;
 CurrentFrame = -1; 
 CurrentPreview = 0;  
 // Erase all old frames
 SystemFile("rm %s%s", "Frames/*");
}
/*
// Erase the just current frame. Renumber the remaining framesso that
// the CurrentFrame increment and decrement will work.
void Erase()
{
 extern int CurrentFrame, FrameCount;   
 int Dex;
 char t[256];

 if(DEBUG) printf("Erasing Frame %d\n", CurrentFrame);
 sprintf(t, "rm %sFrames/Frame%05d.jpg", FULL_PATH, CurrentFrame);
 system(t);
 // With the frame removed, renumber all higher frames
 FrameCount--;
 for(Dex = CurrentFrame; Dex < FrameCount; Dex++)
 {
  sprintf(t, "mv %sFrames/Frame%05d.jpg %sFrames/Frame%05d.jpg", FULL_PATH, Dex+1, FULL_PATH, Dex);
  printf("Rename: %s\n", t);
  system(t);     
 }
}
*/
////////////////////////////////////////////////////////////////////////
//
// Camera Handling
//
// "libcamera-vid -t 0" allows non-recording real-time video but the
// process is not readily stopped. So run it as a forked process and
// stop it as needed with the "kill -kill <pid>" command.
//
////////////////////////////////////////////////////////////////////////

void StartCamera()
{
 extern int *Share;   
 char s[128];

 // Set up a forked process for the camera
 pid_t pid = fork();

 if(pid == 0) // Skip this if it's the parent process
 {
  // Start the camera as a separate process   
  sprintf(s, "libcamera-vid -t 0 --width %d --height %d -f", V_WIDE, V_HIGH); 
  system(s);
  Share[VIDEO_PID] = getpid(); // Save the camera process pid in shared memory
  exit(0);  // This does not kill the camera. May not be needed.
 }
}    

void KillCamera()
{
 extern int *Share;      
 
 char s[64];
 // Build the kill command for the saved pid
 sprintf(s, "kill -kill %d", Share[VIDEO_PID]);
 system(s);
}

////////////////////////////////////////////////////////////////////////
//
// Display Functions used by both animation and saved displays
//
////////////////////////////////////////////////////////////////////////

void PlayVideo(char *Folder)
{
 void SystemFile(char* Command, char *File); 
  
 // feh options are:
 // -F --fullscreen
 // -Z --auto-zoom : zoom to screen size in fullscreen
 // --on-last-slide=quit
 // -p --preload : check for non-loadable images first
 // --slideshow-delay time between slides in seconds
 // --recursive show subfolders as well. Since only Frames is a subfolder
 // this just shows the saved animation.
 // system("feh --quiet --hide-pointer  -Z -p --on-last-slide=quit --slideshow-delay 0.001 --recursive");
 // system("feh --quiet --hide-pointer  -Z -p --on-last-slide=quit --slideshow-delay 0.001 /Frames");
 SystemFile("feh --quiet --hide-pointer  -F -p --on-last-slide=quit --slideshow-delay 0.001 %s%s", Folder);
}

/*
// Display a saved frame. This checks for an existing frame thread
// and kills it then restarts the thread to show the frame.
void ShowFrame(char *Frame)
{
 void KillFrame();   
 int CountFiles(char *Folder);   
    
 extern int *Share;
 char s[128];

 if(DEBUG) printf("In ShowFrame()\n");

 // If there is an active Frame thread, kill it
 if(Share[FRAME_PID] != NO_PID) 
 {
  if(DEBUG) printf ("Killing frame proc %d\n", Share[FRAME_PID]);
  KillFrame();
 }
 // Set up a forked process for the camera
 pid_t pid = fork();

 if(pid == 0) // Skip this if it's the parent process
 {
  Share[FRAME_PID] = getpid(); // Save the camera process pid in shared memory   
  if(DEBUG)printf("Show Frame %s, PID %d\n", Frame, Share[FRAME_PID]);   
  // Show the frame
  sprintf(s, "feh %s%s &", FULL_PATH, Frame);
//  sprintf(s, "convert display -size %dx%d Frames/Frame%05d.jpg &", V_WIDE, V_HIGH, Which);
if(DEBUG) printf("%s\n",s);
  system(s);   
  exit(0);  // May not be needed.
 }
}    
*/

////////////////////////////////////////////////////////////////////////
//
// Utility functions
//
////////////////////////////////////////////////////////////////////////

// Function to read a line from a file
char *GetLine(FILE *f)
{
 static char s[256];
 int i=0;
 
 while(!feof(f) && (s[i++] = getc(f)) != '\n');
 s[i] = '\0'; 

 return s;
}

// Kill a process based on the CMD
void ps_kill(char *p)
{
 void SystemFile(char *Command, char *File); 
  
 char r[256], *s, *t;
 FILE *F;
 int id;
 
 SystemFile("ps a > %s%s", "proc.txt");
 sprintf(r, "%sproc.txt", FULL_PATH); 
 F = fopen(r, "r"); // Open the proc file
 while(!feof(F))
 {
  s = GetLine(F); // Grab each line of the file
  t = strchr(s, ':'); // Advance a pointer to the : in the time field
  if(t != NULL) t = strstr(t, p); // Look for the matching CMD
  if(t != NULL) // If found
  {
   sscanf(s, "%d", &id);          // Get the PID
   sprintf(r, "kill -kill %d", id);  // Set up the kill string
   system(r);                    // Kill the process
   if(DEBUG) printf("Killing %s pid %d\n", p, id);
  }
 }
 fclose(F);                     // Close the proc.txt file
 sprintf(r, "%sproc.txt", FULL_PATH); 
 system(r);         // Delete the proc.txt file
}

void Shutdown()
{
 ps_kill("libcamera-vid");
// ps_kill("main");   
 sleep(6);
 system("sudo halt");
 system("sudo shutdown -h now");
 system("sudo poweroff");
}

/*
// Return the count of the number of files in a folder
// Here used in managing the Saved folder
int CountFiles(char *Folder)
{
 int Count;
 FILE *F;
 char s[80];
 
 if(DEBUG) printf("Counting Files in %s\n", Folder);
 // Create the counting command, sending the 
 // result to Count.txt
 sprintf(s, "ls %s -1 | wc -l > %sCount.txt", FULL_PATH, Folder);
 system(s);                   // Execute it.
 sprintf(s, "%s%sCount.txt", FULL_PATH, Folder);
 F = fopen(s, "r"); // Open the Count file
 fscanf(F, "%d", &Count);     // Read the value into Count
 fclose(F);                   // Close Count.txt
// system("rm Count.txt");      // Delete Count.txt
 return Count;
} 
*/

void ShowPressedButton(int B)
{
 int i;
 
 for(i=0; i<NumButtons; i++) if(Buttons[i] == B)
  printf("Button %s Pressed\n", BText[i]);
}

int kbhit() {
    static const int STDIN = 0;
    static int initialized = 0;

    if (! initialized) {
        // Use termios to turn off line buffering
        struct termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = 1;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

// Sends a system command with a file name in the string
// This ius only needed because realtive file paths, after
// a system cd command to the folder, seem to be unreliable.
// The comman arg will have two adhjacent %s in it for the
// path and name
void SystemFile(char *Command, char *File)
{
 char s[256];

 sprintf(s, Command, FULL_PATH, File);
 printf("SysFile: %s\n", s);
 system(s);
}
