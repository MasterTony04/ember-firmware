/* 
 * File:   main.cpp
 * Author: Richard Greene
 * Port of original 3d.c for Raspberry Pi, with changes needed for BBB 
 * Created on March 6, 2014, 4:54 PM
 */


#ifndef	TRUE
#  define	TRUE  (1==1)
#  define	FALSE (!(TRUE))
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>

#include <cv.h>
#include <ml.h>
#include <cxcore.h>
#include <highgui/highgui_c.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <Hardware.h>
#include <Motor.h>

using namespace std;
using namespace cv;

// Main program globals

// Exposure time for normal layers
int exposureTime = 1000 ;	// 1000 milliseconds

//Exposure time for first layer
int firstExposureTime = 5000 ; //5,000 milliseconds

//Exposure time for calibration layers
int calibrationExposureTime = 3000 ; //3000 milliseconds

//Exposure time for support layers
int supportExposureTime = 250 ; //250 milliseconds

//Exposure time for  perimieter of model layers
int perimeterTime = 1000; //1000 milliseconds

//Number of calibration layers
int numCalLayers = 4 ;

//Number of support layers
int numSupLayers = 6 ;

//Layer Thickness

unsigned int sliceThickness;

//Calibration layer thickness
//int calThickness = 25; //25 microns

// RG additions
IplImage* blackScreen = NULL;
IplImage* image[2] = {NULL, NULL};
const int screenWidth = 1280 ;
const int screenHeight = 800;
const char* windowName = "3dp2";
int nImage = 0;
bool useMotors = true;
int inputPin=45;  //P8-11
char GPIOInputValue[64];
FILE *inputHandle = NULL;

/*
 * failUsage:
 *	Program usage information
 *********************************************************************************
 */

const char *usage = "Usage: %s\n"
        "   [-h]            # This help information\n"
        "   [-x]            # No Arduino\n"
        "   [-f time]       # First layer xposure time in mS\n"
        "   [-c time]       # Calibration layer exposure time in mS\n"
        "   [-s time]       # Support layer exposure time in mS\n"
        "   [-t time]       # Exposure time in mS\n"
        "   [-C layers]     # Number of calibration layers\n"
        "   [-S layers]     # Number of support layers\n"
        "   [-d device]     # Serial Device of Arduino (/dev/ttyACM default for Uno)\n"
        "   [-m microsteps]     # Microstepping factor: 1..6\n"
        "   [-l sliceThickness] # Thickness of each slice in 1/1000 mm\n"
        "   fileNameTemplate    # Template - must have 0000 in it\n";

static void failUsage (char *progName)
{
  fprintf (stderr, usage, progName) ;
  exit (EXIT_FAILURE) ;
}

// Display an image and pause some milliseconds for its display
void showImage(IplImage* image, int pause_msec = 10)
{
    cvShowImage(windowName, image);
    cvWaitKey(pause_msec);
}

/*
 * screenClear:
 *	Clear to black (with initialization of window on first call)
 *********************************************************************************
 */
void screenClear (void)
{
    if(!blackScreen)
    {
        // initialize the all black image
        CvSize imageSize;
        imageSize.width = screenWidth;
        imageSize.height = screenHeight;
        blackScreen = cvCreateImage(imageSize, IPL_DEPTH_8U, 1);
        cvZero(blackScreen);
        cvNamedWindow(windowName, CV_WINDOW_AUTOSIZE);
        showImage(blackScreen, 500); // need long pause first time
    }
    else
        showImage(blackScreen);
}



// load an image from a file, and make its alpha layer visible
IplImage* loadImage(const char* fileName)
{
  IplImage* img  = cvLoadImage(fileName, -1); // need -1 to preserve alpha channel!
  
  if (img == NULL)
  {
    printf ("\nMissing image: %s\n", fileName) ;
    return NULL;
  }
  
  // convert the alpha channel to gray values in the RGB channels
  // assume it's a monochrome image to start with
  // with pure white and black pixels needing no further work
  uint8_t* pixelPtr = (uint8_t*)img->imageData;
  int cn = img->nChannels;
  for(int i = 0; i < img->height; i++)
  {
    int yOffset = i * img->width * cn;
    for(int j = 0; j < img->width; j++)
    {
       int xOffset = j * cn;
       unsigned char alpha = pixelPtr[yOffset + xOffset + 3];
       if(alpha != 0 && alpha != 255)
       {
          pixelPtr[yOffset + xOffset] = alpha; 
          pixelPtr[yOffset + xOffset + 1] = alpha;
          pixelPtr[yOffset + xOffset + 2] = alpha;
       }
    }
  }
  return img;
}
/*
 * videoTest:
 *	Display simple test pattern
 *********************************************************************************
 */

void videoTest (void)
{
    screenClear();
    
    // get the path to this exe
    char buff[1024];
    ssize_t len = ::readlink("/proc/self/exe", buff, sizeof(buff)-1);
    if (len != -1) 
    {
      buff[len] = '\0';
    } else 
    {
        printf("could not get path in which this exe resides\n");
    }

    // remove the exe's name & replace with the test pattern file name
    std:string path = buff;
    int posn = path.find_last_of('/');
    path.resize(posn);
    path.append("/TestPattern.png");
    IplImage* testPat = loadImage(path.c_str());
    if(!testPat)
        return;
    
    showImage(testPat);
    
    // wait for key press
    while(cvWaitKey(100) < 0)
        ;
    
    cvReleaseImage(&testPat); 
}


/*
 * checkTemplate:
 *	Check the supplied filename template and make sure the first file
 *	actually exists!
 *********************************************************************************
 */
void checkTemplate (char *progName, char *filenameTemplate)
{
  int  fd, i ;
  int  found = FALSE ;
  char *p, fileName [1024] ;

  if (strlen (filenameTemplate) < 8)	// 0000.png = 8 chars, so minimum filename
  {
    fprintf (stderr, "%s: Filename template \"%s\" is too short\n", progName, filenameTemplate) ;
    exit (EXIT_FAILURE) ;
  }

  for (p = filenameTemplate, i = 0 ; i < strlen (filenameTemplate) - 4 ; ++i)
    if (strncmp (p, "0000", 4) == 0)
    {
      found = TRUE ;
      break ;
    }
    else
      ++p ;

  if (!found)
  {
    fprintf (stderr, "%s: No 0000 found in filename template \"%s\"\n", progName, filenameTemplate) ;
    exit (EXIT_FAILURE) ;
  }

  *p++ = '%' ; *p++ = '0' ; *p++ = '4' ; *p++ = 'd' ;

  sprintf (fileName, filenameTemplate, 1) ;
  if ((fd = open (fileName, O_RDONLY)) < 0)
  {
    fprintf (stderr, "%s: Unable to open first file: \"%s\": %s\n", progName, fileName, strerror (errno)) ;
    exit (EXIT_FAILURE) ;
  }
  close (fd) ;
}

long getMillis(){
    struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
    // printf("time = %d sec + %ld nsec\n", now.tv_sec, now.tv_nsec);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

// set up a pin as an input
void setupPinInput()
{
    char setValue[4], GPIOInputString[4], GPIODirection[64];
    // setup input
    sprintf(GPIOInputString, "%d", inputPin);
    sprintf(GPIOInputValue, "/sys/class/gpio/gpio%d/value", inputPin);
    sprintf(GPIODirection, "/sys/class/gpio/gpio%d/direction", inputPin);
 
    // Export the pin
    if ((inputHandle = fopen("/sys/class/gpio/export", "ab")) == NULL){
        printf("Unable to export GPIO pin\n");
        exit (EXIT_FAILURE) ;
    }
    strcpy(setValue, GPIOInputString);
    fwrite(&setValue, sizeof(char), 2, inputHandle);
    fclose(inputHandle);
 
    // Set direction of the pin to an input
    if ((inputHandle = fopen(GPIODirection, "rb+")) == NULL){
        printf("Unable to open direction handle\n");
        exit (EXIT_FAILURE) ;
    }
    strcpy(setValue,"in");
    fwrite(&setValue, sizeof(char), 2, inputHandle);
    fclose(inputHandle);   
}

// wait for input pin from the motor board going high
// (unless we're not using motors at all))
char getPinInput()
{
    char getValue[4];

     while(useMotors)
     {
        if ((inputHandle = fopen(GPIOInputValue, "rb+")) == NULL){
            printf("Unable to open input handle\n");
            exit (EXIT_FAILURE) ;
        }
        fread(&getValue, sizeof(char), 1, inputHandle);
        fclose(inputHandle);  

        usleep (100000);
        
        if(getValue[0] == '1')
        {
            usleep (2000000);
            break;
        }
     }
 
  return('@');  
}

/*
 * processImages:
 *	Do all the work to process and print our images
 *********************************************************************************
 */
void processImages (char *progName, char *filenameTemplate, Motor motor)
{
  char fileName [1024] ;
  int   i ;
  unsigned long start = 0L, timeUp, time, cycleTime = 0L;
  int   j = 2 + numCalLayers;
  int   k = j + numSupLayers;
  std:string frameType;

// Pre-load first image

  sprintf (fileName, filenameTemplate, 1) ;
  image[nImage] = loadImage(fileName);
  if(!image)
      return;
  
//Calibration sequence
// Send command to motor board to calibrate the mechanicals
  printf("sending c\n");
  motor.Write(MOTOR_COMMAND, 'c') ;
      
  //usleep (45000000);

// Get serial signal from motor board to signal that it's stopped moving
  printf("awaiting interrupt that signals motor board has stopped moving\n");
  getPinInput();
    
//Cycle through images

    for (i = 2; i < 9999; ++i) {

        // If statements to determine correct exposure time for each layer 
        // First layer

        if (i == 2) {
            time = firstExposureTime;
            frameType = "First Layer";
        }
            //Calibration layers

        else if (i > 2 && i <= j) {
            time = calibrationExposureTime;
            frameType = "Calibration Layer";
        }
            //Support layers

        else if (i > j && i <= k) {
            time = supportExposureTime;
            frameType = "Support Layer";
        }
            //Model layers

        else {
            time = exposureTime;
            frameType = "Model Layer";
        }

// Put the image in-hand on the display

    if(start != 0L)
        cycleTime = getMillis() - start;
    start  = getMillis() ;
    timeUp = start + time ;
    showImage(image[nImage]);
    printf ("\nDisplaying frame: %04d %s thickness:%4d  Cycle time: %4d", i - 1, frameType.c_str(), sliceThickness, cycleTime ) ; fflush (stdout) ;
    
    // keep the screen from going to sleep
    system("echo 0 > /sys/class/graphics/fb0/blank");

// Load the next image
    if(++nImage > 1)
        nImage = 0;
    sprintf (fileName, filenameTemplate, i) ;
    cvReleaseImage(&image[nImage]);
    image[nImage] = loadImage(fileName) ;
    
// Wait for the exposure time to be up

    while (getMillis() < timeUp)
      usleep (1000) ;

// Blank the display
    screenClear ();

    // Send command to Arduino to move the mechanicals  
    if (i == 2)  {
        //Print cycle with rotation and overlift
        printf("\nsending P\n");
        motor.Write(MOTOR_COMMAND, 'P');
    }
    else if (i <= k) {
        printf("\nsending P\n");
        motor.Write(MOTOR_COMMAND, 'P');
    }
    else {
        //Print cycle with rotation and overlift
        printf("\nsending P\n");
        motor.Write(MOTOR_COMMAND, 'P');
    }


// No more images?

    if (image[nImage] == NULL)
    {
      printf("about to send r\n");
      //Rotate Clockwise 90 degrees
      motor.Write(MOTOR_COMMAND, 'r') ;

      getPinInput();
      
      //Home Z Axis
      printf("about to send h\n");
      motor.Write(MOTOR_COMMAND, 'h') ;
      
      printf ("\n\n%s: Out of images at: %d\n", progName, i -1) ;
      break ;
    }
   
 // Wait for the arduino to signal that it's stopped moving
   printf("awaiting motor stop\n");
   getPinInput();
   
     
   // Blit the next image to the screen
   // RG - no need to do anything here, image is already loaded & ready to be shown
   
// Quick abort, on any key press
    int key = cvWaitKey(100);
    if (key > 0)
    {
        printf("key pressed: %d\n", key);
        break ;
    }

  }
  
  cvReleaseImage(&blackScreen); 
  cvReleaseImage(&image[0]);
  cvReleaseImage(&image[1]);
  cvDestroyWindow(windowName);

  printf ("\n\nRun ended after %d frames\n\n", i -1) ;

}


/*
 *********************************************************************************
 * main:
 *********************************************************************************
 */

int main (int argc, char *argv [])
{
  int  uSteps  = 1 ;
  int  opt ;
  char* filenameTemplate ;

  char* device;
 // int sliceThickness = 25 ;
  unsigned int then ;
  int ch ;
  
  while ((opt = getopt (argc, argv, "hxvd:m:t:f:c:s:l:C:S:p:")) != -1)
  {
    switch (opt)
    {
      case 'h':		// Heyalp
	failUsage (argv [0]) ;
	break ;

      case 'x':		// Ignore motors for testing
	useMotors = false ;
	break ;

      case 'v':		// Video Test
	videoTest () ;
	return 0 ;

      case 't':		// Exposure time normal layer
	exposureTime = atoi (optarg) ;
	if ((exposureTime < 0) || (exposureTime > 1800000))
	{
	  fprintf (stderr, "%s: Normal Layer exposure time out of range (100-1800000mS)\n", argv [0]) ;
	  exit (EXIT_FAILURE) ;
	}
	break ;

      case 'p':         // Exposure time perimeter layer
        perimeterTime = atoi (optarg) ;
        if ((perimeterTime < 0) || (perimeterTime > 1800000))
        {
          fprintf (stderr, "%s: Perimeter layer exposure time out of range (100-1800000mS)\n", argv [0]) ;
          exit (EXIT_FAILURE) ;
        }
        break ;

      case 'f':         // Exposure time first layer
        firstExposureTime = atoi (optarg) ;
        if ((firstExposureTime < 0) || (firstExposureTime > 1800000))
        {
          fprintf (stderr, "%s: First layer exposure time out of range (100-1800000mS)\n", argv [0]) ;
          exit (EXIT_FAILURE) ;
        }
        break ;

      case 'c':         // Exposure time calibration layer
        calibrationExposureTime = atoi (optarg) ;
        if ((calibrationExposureTime < 0) || (calibrationExposureTime > 1800000))
        {
          fprintf (stderr, "%s: Calibration layer exposure time out of range (100-1800000mS)\n", argv [0]) ;
          exit (EXIT_FAILURE) ;
        }
        break ;

      case 's':         // Exposure time support layer
        supportExposureTime = atoi (optarg) ;
        if ((supportExposureTime < 0) || (supportExposureTime > 1800000))
        {
          fprintf (stderr, "%s: Support layer exposure time out of range (100-1800000mS)\n", argv [0]) ;
          exit (EXIT_FAILURE) ;
        }
        break ;

      case 'C':         // Number of calibration layers
        numCalLayers = atoi (optarg) ;
        if ((numCalLayers < 0) || (numCalLayers > 20))
        {
          fprintf (stderr, "%s: Number of calibration layers  out of range (1-20)\n", argv [0]) ;
          exit (EXIT_FAILURE) ;
        }
        break ;

      case 'S':         // Number of support layers
        numSupLayers = atoi (optarg) ;
        if ((numSupLayers < 0) || (numSupLayers > 400))
        {
          fprintf (stderr, "%s: Number of support layers  out of range (1-400)\n", argv [0]) ;
          exit (EXIT_FAILURE) ;
        }
        break ;

      case 'd':
	if (*optarg != '/')
	  failUsage (argv [0]) ;

	device = (char*)malloc (strlen (optarg) + 1) ;
	strcpy (device, optarg) ;
	break ;

      case 'm':
	uSteps = atoi (optarg) ;
	if ((uSteps < 1) || (uSteps > 6))
	  failUsage (argv [0]) ;
	break ;

      case 'l':			// Slice thickness
	sliceThickness = atoi (optarg) ;
	if ((sliceThickness > 1000) || (sliceThickness < 25))
	  failUsage (argv [0]) ;
	break ;
    }
  }

  if (optind >= argc)
    failUsage (argv [0]) ;

  filenameTemplate = (char*) malloc (strlen (argv [optind]) + 1) ;
  strcpy (filenameTemplate, argv [optind]) ;

  checkTemplate (argv [0], filenameTemplate) ;
  
  // if 'x' option entered, just use a dummy motor
  Motor motor(useMotors ? MOTOR_SLAVE_ADDRESS : 0xFF);
   
  setupPinInput();
  
  usleep(1000000);
  printf("sending @\n");
  motor.Write(MOTOR_COMMAND, ACK) ;
  
  then = getMillis () + 5000 ;
  while (getMillis () < then)
    if (getPinInput() == ACK)
        break ;
  
  printf("got interrupt\n");
  if (getMillis () >= then)
  {
    fprintf (stderr, "%s: Can't establish comms with motor board\n", argv [0]) ;
    exit (EXIT_FAILURE) ;
  }

  // Send microseteps to the Arduino
  // RG - skip this command as it seems to cause problems
//  char buf1[32];
//  sprintf(buf1, "m%d", uSteps);
//  printf("sending %s\n", buf1);
//  motor.Write(MOTOR_COMMAND, (const unsigned char*)buf1);
//  //motor.Write(MOTOR_COMMAND, uSteps + '0') ;
//  if (getPinInput() != ACK)
//  {
//    fprintf (stderr, "%s: motor board didn't ack. microstep command.\n", argv [0]) ;
//    exit (EXIT_FAILURE) ;
//  }

  // Send slice thickness to Arduino

  char buf[32];
  sprintf(buf, "l%04d", sliceThickness);
  printf("sending %s\n", buf);
  motor.Write(MOTOR_COMMAND, (const unsigned char*)buf);
  if (getPinInput() != ACK)
  {
    fprintf (stderr, "%s: motor board didn't ack. thickness command.\n", argv [0]) ;
    exit (EXIT_FAILURE) ;
  }
  
  screenClear();
  
  printf("about to show images\n");
  processImages(argv [0], filenameTemplate, motor);

  return 0 ;
}


