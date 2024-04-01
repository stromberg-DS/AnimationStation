///////////////////////////////////////////////////////////////////////
//
// Stop Motion Animation
//
// File locations:
// 
// All files are in the /home/pi/projects/Animation folder. Within this
// folder are the single source file, main.c (this file), the object and
// executables, and the makefile. There are two folders: Frames and Saved.
// Frames contains the captured frames of the current video being made
// the frame files are in the format Framennnnn.jpg where the "nnnnn" is
// a 5-digit number indicating the sequence number of the frame. The
// Saved folder contains up to MAX_SAVED saved videos. Each video is
// contained in its own folder with folders named Videonn with nn being
// the two digit number of the saved video. Frame files within the each
// Videonn folder have the same naming as those in the Frames folder.
// The latest saved video is always in Video00 with all other saved
// videos having sequentially higher numbers. Videos in excess of
// MAX_SAVED are deleted. Single frames are about 115 kB meaning 90
// videos of 100 frames each require 1 GB.
//
// Button Implementation:
//
// The GPIO pins are used to read the buttons. Using a positive logic
// (pulling the pin high to indicate a pressed button) gave continual
// false button presses. Instead, the switch commons are connected to
// 3.3 volts and the sensing lines are connected to the NC terminals.
// The GPIO pins are configured with internal pull down resistors so
// when a button is pressed, the sensing line goes low.
//
// The systme has two modes of operation: creating videos and viewing
// saved videos. AllButtons are dual function. Five buttons are used for
// creating videos, four for viewing saved videos and one additional
// for switching between modes.
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
//  Breakout   Button   Wire       Function
//
//   IO18       Back     White       Back
//   IO23       Forward  Red Str     Forward
//   IO24       Play     Green       Play
//   IO25       Record   Orange      Save Frame
//   IO12       Erase    Blue        Erase Frame
//   IO16        --       --         Shutdown
//   IO20       Sound    Green Str   Mode
//   IO21         ?      Red         Restart
//
//  Each of these should be wired to the NO terminal on the microswitch
//  A line from a 3.3 volt pin should connect to all COM terminals
//  on the microswitches.
//
// The pin defines for the buttons follow immediately
// Pin defines for the buttons

#define FRAME_BCK 18  // White
#define FRAME_FWD 23  // Red Stripe
#define PLAY      24  // Green
#define RECORD    25  // Orange
#define ERASE     12  // Blue
#define RESTART   21  // Erase and start over
#define SHUTDOWN  16  // Shutdown program 
#define NO_BUTTON -1

#define SWITCH_MODE 20

// Saved Video Buttons. The defines are duplicated from above
// because of the dual nature of the buttons
#define SAVE        25  // Red
#define VIEW_PREV   18 // Blue
#define PLAY_SAVED  24  // Yellow
#define VIEW_NEXT   23  // Green

// Video Implementation:
// 
// This ended up requiring multiple camera handling packages to work
// properly.
//
// libcamera is the standard Raspberry Pi labrary for handling the
// camera. Here, this is used only to display live video. If it
// is used to grab frames it adds a 5 second delay that is unacceptable
// here.
//
// libcamera command:
// libcamera-vid //shows real time video
// options:
// -t 0 // keep video running until cancelled
// --width nnn --height nnn // set camera resolution
// -p x,y,width,height // set location and size of preview

// Instead, Raspberry Pi's built-in screen shot function scrot is used
// to grab frames from the video. This grabs the entire monitor 
// display but is very quick. Using scrot instead of libcamera-still 
// allows the live video to keep running.
//
// scrot command:
// scrot <output file>
//
// Image Magick is then used to crop the screen shot and save it.
// While online documentation shows an imagemagick command, this 
// does not seem to work. Instead the convert command is used.
//
// ImageMagick command to crop the screenshot is:
// convert <input image> -crop <w>x<h>+<x0>+<y0> <output image>  
// w and h are the cropped size, x0 y0 is the upper left corner of the 
// crop. For cropping, the top gray bar in the video viewer is 30 pixels
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
// To set the screen resolution (800x600 is chosen here to give
// adequate definition and display speed). Got to 
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
#include <ctype.h>  // for toupper
#include <termios.h>  
#include <string.h>
#include <stdlib.h>
#include <unistd.h> // Needeed for sleep() and usleep()
#include <bcm2835.h>
#include <sys/mman.h>
#include <sys/select.h> // Needed for kbhit()
#include <sys/ioctl.h> // Needed for kbhit()
//#include <stropts.h> // Needed for kbhit()
#include <time.h>

#define DEBUG 1
#define USE_KBD 1

// Basic defines
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define RANGE(a, b, c) ((a) > (b) ? (a) : ((c) < (b) ? (c) : (b)))

// Set this to match the screen resolution
#define V_WIDE 1920
#define V_HIGH 1080

#define VIDEO_PID  0
#define FRAME_PID  1
#define NO_PID    -1

#define MAX_SAVED 25         // Maximum number of videos saved 
#define MIN_SAVE_INTERVAL 30 // Minimum time between video saves

#define MODE_CREATE 0
#define MODE_VIEW   1
#define MODE_TIMEOUT 120

// Globals, Assign the button defines to an array to allow button 
// checking in a loop 
int Buttons[] = { FRAME_BCK, FRAME_FWD, PLAY, RECORD, ERASE, RESTART, SHUTDOWN, SWITCH_MODE };
// Name the buttons for use in diagnostic messages
char BText[][32] = { "Back", "Forward" , "Play", "Record", "Erase", "Restart", "Shutdown",  "Mode"};
    
// Figure how many buttons are defined
int NumButtons = sizeof(Buttons)/sizeof(Buttons[0]);
int FrameCount;     // Keeps count of total frames recorded
int CurrentFrame;   // Track current frame location
int CurrentPreview; // Track which video is being previewed

int *Share; // Shared memory to get the camera pid. This allows the
// Continous video started in StartCamera() to be stopped by KillCamera()
// Otherwise the live video can't be stopped

// Last time a button was pressed
time_t LastPress = 0;
int Mode = MODE_CREATE;

int main()
{
 void StartCamera();
 void KillCamera();   
 void KillFrame();
 void Restart();
 void InitGPIO();
 void StartCamera();
 
 int ReadButtons();
 void CheckTimeOut();
 void Erase();
 void ShowFrameInCurrent();
// void ShowNextFrame();
// void ShowPreviousFrame();
 void Play();
 void GrabFrame(int Frame, int Wide, int High);
 void Shutdown();
 
 void ShowPreview();
 void PlaySaved();
 void SaveVideo();

 int CountFiles(char *Folder);
 void ShowPressedButton(int Button);
 
 int B;

 // Allocate the shared memory for the video camera pid
 Share = mmap(NULL, 32, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
 // Set the PIDs to show not forked processes
 Share[VIDEO_PID] = NO_PID;
 Share[FRAME_PID] = NO_PID;
 
 Restart();        // Initialize values
 InitGPIO();       // Start the BCM2835 library to allow reading the buttons
 StartCamera();    // Turn on the live video

 system("cd /home/rpi/projects/Animation");

 while(1)
 {
  if((B = ReadButtons()) != NO_BUTTON) // Look for a button press
  
  {
   ShowPressedButton(B);
   LastPress = time(NULL);             // Reset the inactivity timer 
// This if is for switching between modes
//   if(B == SWITCH_MODE) Mode = Mode == MODE_CREATE ? MODE_VIEW : MODE_CREATE; 
//   if(DEBUG) printf("Mode: %s\n", Mode == MODE_CREATE ? "Create" : "View");

//   if(Mode == MODE_CREATE)
//   { 
    switch(B)
    {
     ////////////////////////////////////////////////////////////////////
     //
     // Animation creation options
     //
     ////////////////////////////////////////////////////////////////////
     
     // Erase the displayed frame from the list
     case ERASE     :  Erase();                                    break;  
      
     // For now the question mark is the restart.
     // Using the erase to erase the displayed frame   
     case RESTART   : Restart();                                   break;
   
     // Return from preview by killing frame process
  //   case SOUND     : KillFrame();                                 break;
     // Back up and show previous frame
     case FRAME_BCK : CurrentFrame--; ShowFrameInCurrent();        break;
  
     // Advance to next frame and display it
     case FRAME_FWD : CurrentFrame++; ShowFrameInCurrent();        break;
 
     // Play the animations 
     case PLAY      : Play();                                      break;
   
     // Grab the current image an put it last in the sequence 
     case RECORD    : GrabFrame(FrameCount, V_WIDE, V_HIGH);       break;
     
     case SHUTDOWN  : Shutdown();                                  break;
    }
//   }
// This "else" is for viewing videos
/*
   else
   {
    switch(B)
    {  
     ////////////////////////////////////////////////////////////////////
     //
     // Animation save and playback options 
     //
     ////////////////////////////////////////////////////////////////////
    
     // Save the current video for later playback
     case RECORD    : SaveVideo();                                 break;  
    
     // Preview previous saved video
     case FRAME_BCK : CurrentPreview--; ShowPreview();             break;
     
     // Preview next saved video
     case FRAME_FWD : CurrentPreview++; ShowPreview();             break;
 
     // Play selected saved video
    case PLAY       : PlaySaved();                                 break;
    }
   }
*/
  }
//  CheckTimeOut(); // This was only for returning to the record menu
                    // after inactivity after viewing videos 
 }
 return 1;
}

///////////////////////////////////////////////////////////////////////
//
// Button handling functions
//
///////////////////////////////////////////////////////////////////////

// Button wires are attached to the NC pin on the microswitches and
// a button press is reaqd as a LOW value. The pins are configured
// with pull down resistors.

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

 extern time_t LastPress;  

 time_t Now = time(NULL);    // Get the current time
 int i, Found = -1;
// char c;

 // bcm2835_gpio_lev(); returns HIGH or LOW
 if(USE_KBD)
 {
  if(!kbhit()) return NO_BUTTON; 
  printf("Key: %c\n", getchar());
  return NO_BUTTON; //Button[ c - '1'];
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
 if(Found != -1 && Now - LastPress > 0) 
 {
  LastPress = Now; // Update the last pressed time to current time
  return Buttons[Found]; // Send the button info back
 }
 if(Now - LastPress > MODE_TIMEOUT) Mode = MODE_CREATE; 
 return NO_BUTTON;
}

///////////////////////////////////////////////////////////////////////
//
// Display Functions
//
///////////////////////////////////////////////////////////////////////

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
/*
 * 
void ShowPreviousFrame()
{
 void ShowFrame(char *Frame); 
  
 extern int FrameCount, CurrentFrame; 
  
 char s[32];
 // Decrement the frame position with wrap around
 if(CurrentFrame <= 0)  CurrentFrame = FrameCount-1;
 else CurrentFrame--;
 
 // Build the frame file name
 sprintf(s, "Frames/Frame%05d.jpg", CurrentFrame);
 // Show the frame
 ShowFrame(s);
}

void ShowNextFrame()
{
 void ShowFrame(char *Frame); 
  
 extern int FrameCount, CurrentFrame; 
  
 char s[32];
 // Increment the frame position with wrap around
 if(CurrentFrame >= FrameCount-1)  CurrentFrame = 0;
 else CurrentFrame++;
 
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

 char s[256];	

// KillCamera();	    
// sprintf(s, "libcamera-jpeg -t 1 -n -o Frames/Frame%05d.jpg --width %d --height %d", n, w, h);
 // Grab a full screen
 system("scrot Scrot.jpg");
 // Crop it and save the result at the Frame n position
 sprintf(s, "convert Scrot.jpg -crop %dx%d+0+30 Frames/Frame%05d.jpg", V_WIDE, V_HIGH, n);
 system(s);
 // Delete the original screen shot
 system("rm Scrot.jpg");
 CurrentFrame++;  // Update the current frame index 
 FrameCount++;    // And the total frame count
 if(DEBUG) printf("Record Frame #%d\n", FrameCount);

// StartCamera();
// ShowFrame(n);
}

// Erase all frames and reset the counters 
void Restart()
{
 // Initialize the counters
 FrameCount = 0;
 CurrentFrame = -1; 
 CurrentPreview = 0;  
 // Erase all old frames
 system ("rm Frames/*");
}

// Erase the just current frame. Renumber the remaining framesso that
// the CurrentFrame increment and decrement will work.
void Erase()
{
 extern int CurrentFrame, FrameCount;   
 int Dex;
 char t[256];

 if(DEBUG) printf("Erasing Frame %d\n", CurrentFrame);
 sprintf(t, "rm Frames/Frame%05d.jpg", CurrentFrame);
 system(t);
 // With the frame removed, renumber all higher frames
 FrameCount--;
 for(Dex = CurrentFrame; Dex < FrameCount; Dex++)
 {
  sprintf(t, "mv Frames/Frame%05d.jpg Frames/Frame%05d.jpg", Dex+1, Dex);
  printf("Rename: %s\n", t);
  system(t);     
 }
}

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
  sprintf(s, "libcamera-vid -t 0 --width %d --height %d", V_WIDE, V_HIGH); 
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
// Saved video functions
//
////////////////////////////////////////////////////////////////////////

void ShowPreview()
{
 int CountFiles(char *Folder);
 void ShowFrame(char *Frame);

 extern int CurrentPreview;
 // See how many saved videos there are
 int Count = CountFiles("Saved");
 char s[128];
 
 if(CurrentPreview < 0) CurrentPreview = Count-1;
 if(CurrentPreview >= Count) CurrentPreview = 0;
 
 sprintf(s, "Saved/Video%02d/Frame00000.jpg", CurrentPreview);
 ShowFrame(s);  
}

void PlaySaved()
{
 void PlayVideo(char *Folder); 
  
 extern int CurrentPreview;
 
 char s[128]; 
  
 sprintf(s, "Saved/Video%02d", CurrentPreview); 
 PlayVideo(s); 
}
 
void SaveVideo()
{
 int CountFiles(char* Folder); 
 
 static time_t LastSave = 0;
 time_t ThisTime = time(NULL);
 int i;
 char Cmd[128];

 if(ThisTime - LastSave < MIN_SAVE_INTERVAL && !DEBUG) return;
 LastSave = ThisTime;

 // See how many saved videos there are
 int Count = CountFiles("Saved");
 if(DEBUG) printf("%d Saved Videos found\n", Count);

 // Move all existing videos up one slot  
 for(i=Count; i>0; i--)
 {
  sprintf(Cmd, "mv Saved/Video%02d Video%02d", i-1, i); 
  printf("Cmd: %s\n", Cmd);
  system(Cmd);
 }

 // If the limit has been reached, delete the oldest file
 if(Count == MAX_SAVED)
 {
  sprintf(Cmd, "rm Saved/Video%02d", MAX_SAVED); 
  system(Cmd); 
 }
 // Save the new video as Video00 
 // The mv command should work as well but it places the Frames
 // folder, not just the files into the Video00 folder so
 // use rsync instead.
 system("rsync -a Frames/ Saved/Video00/");
}

////////////////////////////////////////////////////////////////////////
//
// Display Functions used by both animation and saved displays
//
////////////////////////////////////////////////////////////////////////

void PlayVideo(char *Folder)
{
 char s[128];
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
 sprintf(s, "feh --quiet --hide-pointer  -Z -p --on-last-slide=quit --slideshow-delay 0.001 %s", Folder);
 system(s);
}

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
  sprintf(s, "feh %s &", Frame);
//  sprintf(s, "convert display -size %dx%d Frames/Frame%05d.jpg &", V_WIDE, V_HIGH, Which);
if(DEBUG) printf("%s\n",s);
  system(s);   
  exit(0);  // May not be needed.
 }
}    

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
 char r[80], *s, *t;
 FILE *F;
 int id;
 
 system("ps a > proc.txt"); // Send proc info the proc.txt
 F = fopen("proc.txt", "r"); // Open the proc file
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
 system("rm proc.txt");         // Delete the proc.txt file
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
 sprintf(s, "ls %s -1 | wc -l > Count.txt", Folder);
 system(s);                   // Execute it.
 F = fopen("Count.txt", "r"); // Open the Count file
 fscanf(F, "%d", &Count);     // Read the value into Count
 fclose(F);                   // Close Count.txt
// system("rm Count.txt");      // Delete Count.txt
 return Count;
} 

void CheckTimeOut()
{
 time_t Now = time(NULL);
 
 if(Now - LastPress >  MODE_TIMEOUT) Mode = MODE_CREATE;
}

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
