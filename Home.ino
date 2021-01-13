// -----------------------------------------------------------------------------------
// Functions related to Homing the mount

#if (HOME_SENSE != OFF)
enum findHomeModes { FH_OFF,FH_FAST,FH_IDLE,FH_SLOW,FH_DONE };
findHomeModes findHomeMode=FH_OFF;
int PierSideStateAxis1=LOW;
int PierSideStateAxis2=LOW;
unsigned long findHomeTimeout=0L;

void checkHome() {
  // check if find home timed out or stopped
  if (findHomeMode == FH_FAST || findHomeMode == FH_SLOW) {
    if ((long)(millis()-findHomeTimeout) > 0L || (!guideDirAxis1 && !guideDirAxis2)) {
      if ((long)(millis()-findHomeTimeout) > 0L) { generalError=ERR_LIMIT_SENSE; VLF("MSG: Homing timed out, stopping guides"); }
      stopGuideAxis1();
      stopGuideAxis2();
      safetyLimitsOn=true;
      findHomeMode=FH_OFF;
    } else {
      if (axis1HomeSense.read() != PierSideStateAxis1 && (guideDirAxis1 == 'e' || guideDirAxis1 == 'w')) StopAxis1();
      if (axis2HomeSense.read() != PierSideStateAxis2 && (guideDirAxis2 == 'n' || guideDirAxis2 == 's')) StopAxis2();
    }
  }
  // we are idle and waiting for a fast guide to stop before the final slow guide to refine the home position
  if (findHomeMode == FH_IDLE && guideDirAxis1 == 0 && guideDirAxis2 == 0) {
    findHomeMode=FH_OFF;
    goHome(false);
  }
  // we are finishing off the find home
  if (findHomeMode == FH_DONE && guideDirAxis1 == 0 && guideDirAxis2 == 0) {
    findHomeMode=FH_OFF;

    VLF("MSG: Homing done");
    if (AXIS2_TANGENT_ARM == ON) {
      trackingState=abortTrackingState;
      cli();
      targetAxis2.part.m = 0; targetAxis2.part.f = 0;
      posAxis2           = 0;
      sei();
    } else {
      // at the home position
      initStartPosition();
      atHome=true;
    }
  }
}

void StopAxis1() {
  stopGuideAxis1();
  VLF("MSG: Homing switch detected, stopping guide on Axis1");
  if (guideDirAxis2 != 'n' && guideDirAxis2 != 's') { if (findHomeMode == FH_SLOW) findHomeMode=FH_DONE; if (findHomeMode == FH_FAST) findHomeMode=FH_IDLE; }
}

void StopAxis2() {
  stopGuideAxis2();
  VLF("MSG: Homing switch detected, stopping guide on Axis2");
  if (guideDirAxis1 != 'e' && guideDirAxis1 != 'w') { if (findHomeMode == FH_SLOW) findHomeMode=FH_DONE; if (findHomeMode == FH_FAST) findHomeMode=FH_IDLE; }
}
#endif

// moves telescope to the home position, then stops tracking
CommandErrors goHome(bool fast) {
  CommandErrors e=validateGoto();
  
#if HOME_SENSE != OFF
  if (e != CE_NONE && e != CE_SLEW_ERR_IN_STANDBY) return e;

  if (findHomeMode != FH_OFF) return CE_MOUNT_IN_MOTION;

  // stop tracking
  abortTrackingState=trackingState;
  trackingState=TrackingNone;

  // reset/read initial state
  axis1HomeSense.reset(); PierSideStateAxis1=axis1HomeSense.read();
  axis2HomeSense.reset(); PierSideStateAxis2=axis2HomeSense.read();

  // decide direction to guide
  char a1; if (PierSideStateAxis1 == HOME_SENSE_STATE_AXIS1) a1='w'; else a1='e';
  char a2; if (PierSideStateAxis2 == HOME_SENSE_STATE_AXIS2) a2='n'; else a2='s';

  // disable limits
  safetyLimitsOn=false;

  // start guides
  if (fast) {
    // make sure tracking is disabled
    if (AXIS2_TANGENT_ARM == OFF) trackingState=TrackingNone;

    // make sure motors are powered on
    enableStepperDrivers();

    findHomeMode=FH_FAST;
    double secPerDeg=3600.0/(double)guideRates[8];
    findHomeTimeout=millis()+(unsigned long)(secPerDeg*180.0*1000.0);
    
    // 8=HalfMaxRate
    if (AXIS2_TANGENT_ARM == OFF) e=startGuideAxis1(a1,8,0,false);
    if (e == CE_NONE) e=startGuideAxis2(a2,8,0,false,true);
    if (e == CE_NONE) VLF("MSG: Homing started phase 1"); else VLF("MSG: Homing start phase 1 failed");
  } else {
    findHomeMode=FH_SLOW;
    findHomeTimeout=millis()+30000UL;
    
    // 7=48x sidereal
    if (AXIS2_TANGENT_ARM == OFF) e=startGuideAxis1(a1,7,0,false);
    if (e == CE_NONE) e=startGuideAxis2(a2,7,0,false,true);
    if (e == CE_NONE) VLF("MSG: Homing started phase 2"); } else VLF("MSG: Homing start phase 1 failed");
  }
  if (e != CE_NONE) stopSlewingAndTracking(SS_ALL_FAST);
  return e;
#else
  if (e != CE_NONE) return e; 

  abortTrackingState=trackingState;

  if (AXIS2_TANGENT_ARM == ON) {
    double h=getInstrAxis1();
    double i2=indexAxis2;
    int p=getInstrPierSide();
    if (latitude >= 0) { if (p == PIER_SIDE_WEST) i2=180.0-i2; } else { if (p == PIER_SIDE_WEST) i2=-180.0-i2; }
    e=goTo(h,i2,h,i2,p,false);
  } else {
    trackingState=TrackingNone;
    e=goTo(homePositionAxis1,homePositionAxis2,homePositionAxis1,homePositionAxis2,PIER_SIDE_EAST,false);
  }

  if (e == CE_NONE) { VLF("MSG: Homing started"); homeMount=true; } else { VLF("MSG: Homing failed"); trackingState=abortTrackingState; }
  return e;
#endif
}

bool isHoming() {
#if HOME_SENSE != OFF
  return (homeMount || (findHomeMode != FH_OFF));
#else
  return homeMount;
#endif
}

// sets telescope home position; user manually moves to Hour Angle 90 and Declination 90 (CWD position),
// then the first gotoEqu will set the pier side and turn on tracking
CommandErrors setHome() {
  if (isSlewing()) return CE_MOUNT_IN_MOTION;

  // back to startup state
  reactivateBacklashComp();
  initStartupValues();
  initStartPosition();

  currentAlt=45.0;
  doFastAltCalc(true);

  safetyLimitsOn=true;

  // initialize and disable the stepper drivers
  StepperModeTrackingInit();
 
  // not parked, but don't wipe the park position if it's saved - we can still use it
  parkStatus=NotParked;
  nv.write(EE_parkStatus,parkStatus);
  
  // reset PEC, unless we have an index to recover from this
  pecRecorded=nv.read(EE_pecRecorded);
  if (pecRecorded != true && pecRecorded != false) { pecRecorded=false; DLF("ERR, setHome(): bad NV pecRecorded"); }
  #if PEC_SENSE == OFF
    pecStatus=IgnorePEC;
    nv.write(EE_pecStatus,pecStatus);
  #else
    pecStatus=nv.read(EE_pecStatus);
    if (pecStatus < PEC_STATUS_FIRST || pecStatus > PEC_STATUS_LAST) { pecStatus=IgnorePEC; DLF("ERR, setHome(): bad NV pecStatus"); }
  #endif
  if (!pecRecorded) pecStatus=IgnorePEC;

  return CE_NONE;
}
