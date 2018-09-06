/*

  Project Name:   Mine Mapping and 3D reconstruction of the mine
  Author List:    Ajit Mutalik
  Filename:     Arduino Firmware
  Functions:    dmpDataReady, cmdVelCB,timerIsr,setup,loop ,do_count
  Global Variables: LF_enc , LR_enc , RF_enc , RR_enc , LF_pwm_pin , LR_pwm_pin , RF_pwm_pin , RR_pwm_pin , LF_DIR , LR_BRK , RF_DIR , RF_BRK , RR_DIR , RR_BRK , LOOP_TIME ,
                    frameid[] , child[] , cpr , wheel_circumference , count_LF , count_LR , count_RF , count_RR , distance_LF , distance_LR , distance_RF , distance_RR ,
                    dmpReady , mpuIntStatus , devStatus , packetSize , fifoCount , fifoBuffer[64] , euler[3] , ypr[3].

  terminologies used 
      
    //LF=left forward motor.
    //LR=left forward motor.
    //RF=left forward motor.
    //LF=left forward motor.


*/

/*
  While the core parts of ROS are licensed under the BSD license, other licenses are commonly used in the community packages,
  such as the Apache 2.0 license, the GPL license, the MIT license, and even proprietary licenses.

*/

/*

   Please take a note code wont compile if these required arduino ide libraires are missing
      - I2Cdev
      - ros_lib (generated using rosserial package)
      - the r2d2 encoder is our own custom message header


  Libraries Used of ROS



*/




//Main ROS header file for node handling
#include <ros.h>
//ROS timer library for keeping timestamp
#include <ros/time.h>
//For I2c communication between arduino and mpu
#include "I2Cdev.h"
//Mpu header file
#include "MPU6050_6Axis_MotionApps20.h"
//Transform variable for bradcasting the transformation
#include <tf/transform_broadcaster.h>
//Quaternion variable for orientation
#include <geometry_msgs/Quaternion.h>
//Vector3 variable for angular and linear acclearation
#include <geometry_msgs/Vector3.h>
//Standard ROS message of type float 32-bit long.
#include <std_msgs/Float32.h>
//Object to hold incoming mesages for telelop.
#include <geometry_msgs/Twist.h>
//Library for ISR.
#include <TimerOne.h>
//Custom message for encoder data.
#include <r2d2/encoder.h>

//Time interval for looping the ISR.
#define LOOP_TIME 1000000  //the time decalred is in microseconds

//object handler of the mpu 6050
MPU6050 mpu;




//defining the Encoder pins
#define LF_enc 30
#define LR_enc 31
#define RF_enc 34
#define RR_enc 33


//defining of the pwm pins
#define LF_pwm_pin 4
#define LR_pwm_pin 5
#define RF_pwm_pin 6
#define RR_pwm_pin 7

//Note That the motor driver which we are using doesn't have H bridge switching , instead it has a direction pin "DIR" for switching the direction of the motor

//defining the left forward wheel pins
#define LF_DIR 42
#define LF_BRK 43

//defining the left rear wheel pins
#define LR_DIR 44
#define LR_BRK 45

//defining the Right forward wheel pins
#define RF_DIR 46
#define RF_BRK 47


//defining the right rear wheel pins
#define RR_DIR 48
#define RR_BRK 49

//creating the ros node handler
ros::NodeHandle nh;

//creating a encoder data message handler
r2d2::encoder encoder_data;
ros::Publisher pub_range( "encoder", &encoder_data);

//creating a orientation data message handler
geometry_msgs::Quaternion orientation;
ros::Publisher imu_ori("imu_orientation", &orientation);

//creating a angular velocity data message handler
geometry_msgs::Vector3 angular_velocity;
ros::Publisher imu_gyr("imu_gyro", &angular_velocity);

//creating a linear acclearation data message handler
geometry_msgs::Vector3 linear_acceleration;
ros::Publisher imu_acc("imu_accl", &linear_acceleration);

geometry_msgs::TransformStamped t;
tf::TransformBroadcaster broadcaster;


//creating frames of the rover body

char frameid[] = "/base_link";
char child[] = "/imu_frame";

//Creating the variables depending on the bots dimensions for encoders
/*
  Wheel dimensions
    Tyre outer diameter: Approx. 120mm/ 4.72inch
    Tyre width: Approx. 70mm/ 2.75inch
    Wheel rim diameter: Approx. 58mm/ 2.28inch
    Hexagonal adapter: Approx. 12mm/0.47inch
    wheel circumference = 37.69908 cm
*/
float cpr = 14760;  //counts per revolution of the quadrature encoder 
float wheel_circumference = 37.69908;

//variables for the keeping the counts of the encoders

float count_LF = 0;
float count_LR = 0;
float count_RF = 0;
float count_RR = 0;

//variables for storing distance travelled by all the wheels
float distance_LF;
float distance_LR;
float distance_RF;
float distance_RR;




//creating the test for checking whether correct data is being sent for debugging

bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector


/*
  *
  * Function Name:  dmpDataReady
  * Input:    -
  * Output:   Sets the mpu interrupt to true
  * Logic:    Check if the mpu is ready to transmit the data
  *
  */

volatile bool mpuInterrupt = false;
void dmpDataReady() {
  mpuInterrupt = true;
}



/*

  Function Name:  cmdVelCB

  Input: [lx,ly,lz,az]

  We will be using only x in linear accelaration and z in angular accelaration
  linear accelaration
     x: (-1 to 1) if positive rover will move in forward direction if negative rover will be moving in backward direction
     y: This variable will be set to zero as we wont require it
     z: This variable will be set to zero as we wont require it

  angular accelaration
    z: (-1 to 1) if positive the rover will rotate in anti clockwise direction and if negative the rover will move in clockwise direction


  Output:    Movements of the bot

  Logic:  Since our rover is four wheel differential drive, rover will be moving in the 2D space. So we will be using only linear motion on x axis that is forward and backward
          and angular rotation along z axis for turning clockwise and anti clockwise (Spot Turning). The variables ranges from -1 to 1 , these are float variables. Depending upon 
          the value from teleop node variables, Arduino will decide the movement of the bot

  Requirement : teleop_twist_keyboard node should be running the arduino will listen to the variables

  Example Call:   If the variables are set this way


        eg 1 :- [1,0,0,0] the bot will move forward
        eg 2 :- [-1,0,0,0] the bot will move backward
        eg 2 :- [0,0,0,1] the bot will rotate anti clockwise direction
        eg 2 :- [0,0,0,-1] the bot will rotate clockwise direction

        Both linear x and angular z can be set together for curved turning by using PID but it is yet to be implemented
*/






//Callback function gets execute after subscribing to the cmd_vel topic 
void cmdVelCB( const geometry_msgs::Twist& twist)
{

  float linear_x = twist.linear.x;
  float angular_z = twist.angular.z;
  //creating a pwm variable so that it can be changed dynamically
  int pwm_LF;
  int pwm_LR;
  int pwm_RF;
  int pwm_RR;

  /*

     Making if else if else conditions for setting up the directions and pwm of the motors according to the instructions recived through the "teleop twist keyboard" node ,


  */
  if (linear_x == 0 && angular_z == 0) {

    //setting up the directions , setting it LOW though the default is LOW just for assurance
    digitalWrite(LF_DIR, LOW);
    digitalWrite(LR_DIR, LOW);
    digitalWrite(RF_DIR, LOW);
    digitalWrite(RR_DIR, LOW);

    //setting up the pwm , setting it all to zero as bot will be stationary in this situation
    pwm_LF = 0;
    pwm_LR = 0;
    pwm_RF = 0;
    pwm_RR = 0;
  }



  else if (angular_z == 0 && linear_x != 0) {

    //Now in this condition there are two cases that the bot will move forward of backward

    //so two conditions

    //It moves in the forward direction , setting up the direction
    if (linear_x < 0) {
      digitalWrite(LF_DIR, LOW);
      digitalWrite(LR_DIR, LOW);
      digitalWrite(RF_DIR, LOW);
      digitalWrite(RR_DIR, LOW);

      // setting up the pwm of the motors
      pwm_LF = 245;
      pwm_LR = 245;
      pwm_RF = 245;
      pwm_RR = 245;

    }
    //It moves in the backward direction
    else if (linear_x > 0) {
      digitalWrite(LF_DIR, HIGH);
      digitalWrite(LR_DIR, HIGH);
      digitalWrite(RF_DIR, HIGH);
      digitalWrite(RR_DIR, HIGH);


      // setting up the pwm of the motors
      pwm_LF = 245;
      pwm_LR = 245;
      pwm_RF = 245;
      pwm_RR = 245;

    }

    //setting default conditions if the tests fails
    else {
      //setting the direction
      digitalWrite(LF_DIR, LOW);
      digitalWrite(LR_DIR, LOW);
      digitalWrite(RF_DIR, LOW);
      digitalWrite(RR_DIR, LOW);

      //setting up the pwm , setting it all to zero as bot will be stationary in this situation
      pwm_LF = 0;
      pwm_LR = 0;
      pwm_RF = 0;
      pwm_RR = 0;

    }
  }



  //For Spot turning

  else if (angular_z != 0 && linear_x == 0) {

    // if the angular z is positive that means it will rotate in left or anti clockwise direction
    if (angular_z > 0) {

      //setting up the directions
      digitalWrite(LF_DIR, LOW);
      digitalWrite(LR_DIR, LOW);
      digitalWrite(RF_DIR, HIGH);
      digitalWrite(RR_DIR, HIGH);

      //setting up the pwm since its spot turning all pwms will be same
      pwm_LF = 245;
      pwm_LR = 245;
      pwm_RF = 245;
      pwm_RR = 245;

    }

    // if the angular z is negative it will rotate in right or clockwise direction
    else if (angular_z < 0) {
      //setting up the directions
      digitalWrite(LF_DIR, HIGH);
      digitalWrite(LR_DIR, HIGH);
      digitalWrite(RF_DIR, LOW);
      digitalWrite(RR_DIR, LOW);

      //setting up the pwm since its spot turning all pwms will be same
      pwm_LF = 245;
      pwm_LR = 245;
      pwm_RF = 245;
      pwm_RR = 245;

    }
    //setting default conditions if the tests fails
    else {
      //setting the direction
      digitalWrite(LF_DIR, LOW);
      digitalWrite(LR_DIR, LOW);
      digitalWrite(RF_DIR, LOW);
      digitalWrite(RR_DIR, LOW);

      //setting up the pwm , setting it all to zero as bot will be stationary in this situation
      pwm_LF = 0;
      pwm_LR = 0;
      pwm_RF = 0;
      pwm_RR = 0;

    }

  }



  /*

    Here the angular part also comes in to play so the pwm has to be set dynamically for the curved turnings

    Here i have divided the part in two phases
    - One without the PID
    - One with PID

    This part of the code is still under development

  */


  //  else if () {
  //
  //
  //  }

  else {

    //if all the above conditions fails this else condition will set it back the default condition

    //setting the direction
    digitalWrite(LF_DIR, LOW);
    digitalWrite(LR_DIR, LOW);
    digitalWrite(RF_DIR, LOW);
    digitalWrite(RR_DIR, LOW);

    //setting up the pwm , setting it all to zero as bot will be stationary in this situation
    pwm_LF = 0;
    pwm_LR = 0;
    pwm_RF = 0;
    pwm_RR = 0;


  }

  analogWrite(LF_pwm_pin, pwm_LF);
  analogWrite(LR_pwm_pin, pwm_LR);
  analogWrite(RF_pwm_pin, pwm_RF);
  analogWrite(RR_pwm_pin, pwm_RR);



}
//creating the subscriber objecct with a callback function
ros::Subscriber<geometry_msgs::Twist> subCmdVel("cmd_vel", cmdVelCB);


/*
  *
  * Function Name:  do_count
  * Input:    -
  * Output:   void type
  * Logic:    Increments the counter everytime the output of the encoder goes low
  * Example Call:   do_count();
  *
  */
    


//functions that counts the rotations
void do_count()
{
  if (digitalRead(LF_enc) == LOW)
  {
    count_LF++;
  }
  if (digitalRead(LR_enc) == LOW)
  {
    count_LR++;
  }
  if (digitalRead(RF_enc) == LOW)
  {
    count_RF++;
  }
  if (digitalRead(RR_enc) == LOW)
  {
    count_RR++;
  }
}


/*
  *
  * Function Name:  timerIsr
  * Input:   -
  * Output:    Publishes the Encoder count to the ROS node "/tf"
  * Logic:    This function is called after some time interval while the loop fumction is being executed 
  *
  */


//this function publishes the encoder data
void timerIsr()
{
  Timer1.detachInterrupt(); //removes the interrupt give to it in previous state

  //Calculating the distance travelled by each the motor
//  distance_LF =  count_LF;
//  distance_LR =  count_LR;
//  distance_RF =  count_RF;
//  distance_RR = count_RR;


//assigning the data to the ros node variables
  encoder_data.distance_LF = count_LF;
  encoder_data.distance_LR = count_LR;
  encoder_data.distance_RF = count_RF;
  encoder_data.distance_RR = count_RR;


}


void setup() {


#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
  Wire.begin();
  //Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
#elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
  Fastwire::setup(400, true);
#endif

  //initialization of the arduino node
  nh.initNode();
   broadcaster.init(nh);
  //broadcaster.init(nh);
  //advertising the topics in ros master
  nh.advertise(imu_ori);
  nh.advertise(imu_gyr);
  nh.advertise(imu_acc);
  nh.advertise(pub_range);

  //initalizing the mpu
  mpu.initialize(); 
  devStatus = mpu.dmpInitialize();
  //cheching the mpu status 
  if (devStatus == 0) {
    mpu.setDMPEnabled(true);
    //interrupt for the mpu
    attachInterrupt(1, dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize(); //getting the packet size of the data from mpu 
  }


  //setting all pins mode
  pinMode(LF_pwm_pin, OUTPUT);
  pinMode(LR_pwm_pin, OUTPUT);
  pinMode(RF_pwm_pin, OUTPUT);
  pinMode(RR_pwm_pin, OUTPUT);

  pinMode(LF_DIR, OUTPUT);
  pinMode(LF_BRK, OUTPUT);

  pinMode(LR_DIR, OUTPUT);
  pinMode(LR_BRK, OUTPUT);  

  pinMode(RF_DIR, OUTPUT);
  pinMode(RF_BRK, OUTPUT);

  pinMode(RR_DIR, OUTPUT);
  pinMode(RR_BRK, OUTPUT);

  Timer1.initialize(LOOP_TIME); //initializing the Timer1 and giving it parameter of time to loop
  
  //Giving the single interrupt to the all the encoder
  attachInterrupt(0 , do_count, RISING);
  //initialization of the arduino node
  nh.initNode();
  //subscribing to the cmd_vel topic in ros master
  nh.subscribe(subCmdVel);

  //setting the timer1 interrupt
  Timer1.attachInterrupt(timerIsr);
}

void loop() {
  // put your main code here, to run repeatedly:
//  Timer1.attachInterrupt(timerIsr); //attaching the interrupt and giving it a ISR as parameter
 
  //checking wether mpu is ready to transmit the data
  if (!dmpReady) return;

  while (!mpuInterrupt && fifoCount < packetSize) {}

  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();
  //getting the count of data from the 
  fifoCount = mpu.getFIFOCount();

  if ((mpuIntStatus & 0x10) || fifoCount == 1024)
  {
    mpu.resetFIFO();
  }
  else if (mpuIntStatus & 0x01)
  {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();

    mpu.getFIFOBytes(fifoBuffer, packetSize);

    fifoCount -= packetSize;

    //Setting the Data of mpu from fifobuffer to the declared variables
    //Qauternion Message
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    //Gravity Vector messages 
    mpu.dmpGetGravity(&gravity, &q);
    //Yaw , Pitch , Roll data
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    //Accelaration without any real world forces data from mpu
    mpu.dmpGetAccel(&aa, fifoBuffer);
    //Real World accelaration data
    mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
    //            mpu.dmpGetLinearAccelInWorld(&aaWorld, &aaReal, &q);

    //Setting the acquired data to the ROS node variables
    orientation.x = q.x;
    orientation.y = q.y;
    orientation.z = q.z;
    orientation.w = q.w;
    //publishing the assigned data
    
// 


     //Setting the acquired data to the ROS node variables
    angular_velocity.x = ypr[0];
    angular_velocity.y = ypr[1];
    angular_velocity.z = ypr[2];
    //publishing the assigned data

    //Setting the acquired data to the ROS node variables
    linear_acceleration.x = aaReal.x * 1 / 16384. * 9.80665;
    linear_acceleration.y = aaReal.y * 1 / 16384. * 9.80665;
    linear_acceleration.z = aaReal.z * 1 / 16384. * 9.80665;
    //publishing the assigned data
    
//    nh.spinOnce();

                t.header.frame_id = frameid;
                t.child_frame_id = child;
                t.transform.translation.x = 0.5;
                t.transform.rotation.x = q.x;
                t.transform.rotation.y = q.y;
                t.transform.rotation.z = q.z;
                t.transform.rotation.w = q.w;
                t.header.stamp = nh.now();
                broadcaster.sendTransform(t);
                delay(200);     
  }

  
  imu_acc.publish(&linear_acceleration);
  imu_gyr.publish(&angular_velocity);
  imu_ori.publish(&orientation);
  //publishing the data to the ros master
  pub_range.publish( &encoder_data);
  nh.spinOnce();
}
