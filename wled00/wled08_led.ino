/*
 * LED methods
 */
void setValuesFromMainSeg()
{
  WS2812FX::Segment& seg = strip.getSegment(strip.getMainSegmentId());
  colorFromUint32(seg.colors[0]);
  colorFromUint32(seg.colors[1], true);
  effectCurrent = seg.mode;
  effectSpeed = seg.speed;
  effectIntensity = seg.intensity;
  effectPalette = seg.palette;
}


void resetTimebase()
{
  strip.timebase = 0 - millis();
}


void toggleOnOff()
{
  if (bri == 0)
  {
    bri = briLast;
  } else
  {
    briLast = bri;
    bri = 0;
  }
}


void setAllLeds() {
  if (!realtimeMode || !arlsForceMaxBri)
  {
    double d = briT*briMultiplier;
    int val = d/100;
    if (val > 255) val = 255;
    strip.setBrightness(val);
  }
  if (!enableSecTransition)
  {
    for (byte i = 0; i<4; i++)
    {
      colSecT[i] = colSec[i];
    }
  }
  if (useRGBW && autoRGBtoRGBW)
  {
    colorRGBtoRGBW(colT);
    colorRGBtoRGBW(colSecT);
  }
  strip.setColor(0, colT[0], colT[1], colT[2], colT[3]);
  strip.setColor(1, colSecT[0], colSecT[1], colSecT[2], colSecT[3]);
}


void setLedsStandard()
{
  for (byte i=0; i<4; i++)
  {
    colOld[i] = col[i];
    colT[i] = col[i];
    colSecOld[i] = colSec[i];
    colSecT[i] = colSec[i];
  }
  briOld = bri;
  briT = bri;
  setAllLeds();
}


bool colorChanged()
{
  for (byte i=0; i<4; i++)
  {
    if (col[i] != colIT[i]) return true;
    if (colSec[i] != colSecIT[i]) return true;
  }
  if (bri != briIT) return true;
  return false;
}


void colorUpdated(int callMode)
{
  //call for notifier -> 0: init 1: direct change 2: button 3: notification 4: nightlight 5: other (No notification)
  //                     6: fx changed 7: hue 8: preset cycle 9: blynk 10: alexa
  if (callMode != 0 && callMode != 1 && callMode != 5) strip.applyToAllSelected = true; //if not from JSON api, which directly sets segments
  
  bool fxChanged = strip.setEffectConfig(effectCurrent, effectSpeed, effectIntensity, effectPalette);
  if (!colorChanged())
  {
    if (nightlightActive && !nightlightActiveOld && callMode != 3 && callMode != 5)
    {
      notify(4); interfaceUpdateCallMode = 4; return;
    }
    else if (fxChanged) {
      notify(6);
      if (callMode != 8) interfaceUpdateCallMode = 6;
      if (realtimeTimeout == UINT32_MAX) realtimeTimeout = 0;
      if (isPreset) {isPreset = false;}
          else {currentPreset = -1;}
    }
    return; //no change
  }
  if (realtimeTimeout == UINT32_MAX) realtimeTimeout = 0;
  if (isPreset) {isPreset = false;}
      else {currentPreset = -1;}
  if (callMode != 5 && nightlightActive && nightlightFade)
  {
    briNlT = bri;
    nightlightDelayMs -= (millis() - nightlightStartTime);
    nightlightStartTime = millis();
  }
  for (byte i=0; i<4; i++)
  {
    colIT[i] = col[i];
    colSecIT[i] = colSec[i];
  }
  if (briT == 0 && callMode != 3) resetTimebase(); 
  briIT = bri;
  if (bri > 0) briLast = bri;
  
  notify(callMode);
  
  if (fadeTransition)
  {
    //set correct delay if not using notification delay
    if (callMode != 3 && !jsonTransitionOnce) transitionDelayTemp = transitionDelay;
    jsonTransitionOnce = false;
    if (transitionDelayTemp == 0) {setLedsStandard(); strip.trigger(); return;}
    
    if (transitionActive)
    {
      for (byte i=0; i<4; i++)
      {
        colOld[i] = colT[i];
        colSecOld[i] = colSecT[i];
      }
      briOld = briT;
      tperLast = 0;
    }
    strip.setTransitionMode(true);
    transitionActive = true;
    transitionStartTime = millis();
  } else
  {
    setLedsStandard();
    strip.trigger();
  }

  if (callMode == 8) return;
  //set flag to update blynk and mqtt
  interfaceUpdateCallMode = callMode;
}


void updateInterfaces(uint8_t callMode)
{
  #ifndef WLED_DISABLE_ALEXA
  if (espalexaDevice != nullptr && callMode != 10) {
    espalexaDevice->setValue(bri);
    espalexaDevice->setColor(col[0], col[1], col[2]);
  }
  #endif
  if (callMode != 9 && callMode != 5) updateBlynk();
  doPublishMqtt = true;
  lastInterfaceUpdate = millis();
}


void handleTransitions()
{
  //handle still pending interface update
  if (interfaceUpdateCallMode && millis() - lastInterfaceUpdate > 2000)
  {
    updateInterfaces(interfaceUpdateCallMode);
    interfaceUpdateCallMode = 0; //disable
  }
  if (doPublishMqtt) publishMqtt();
  
  if (transitionActive && transitionDelayTemp > 0)
  {
    float tper = (millis() - transitionStartTime)/(float)transitionDelayTemp;
    if (tper >= 1.0)
    {
      strip.setTransitionMode(false);
      transitionActive = false;
      tperLast = 0;
      setLedsStandard();
      return;
    }
    if (tper - tperLast < 0.004) return;
    tperLast = tper;
    for (byte i=0; i<4; i++)
    {
      colT[i] = colOld[i]+((col[i] - colOld[i])*tper);
      colSecT[i] = colSecOld[i]+((colSec[i] - colSecOld[i])*tper);
    }
    briT    = briOld   +((bri    - briOld   )*tper);
    
    setAllLeds();
  }
}


void handleNightlight()
{
  if (nightlightActive)
  {
    if (!nightlightActiveOld) //init
    {
      nightlightStartTime = millis();
      nightlightDelayMs = (int)(nightlightDelayMins*60000);
      nightlightActiveOld = true;
      briNlT = bri;
      for (byte i=0; i<4; i++) colNlT[i] = col[i];                                    // remember starting color
    }
    float nper = (millis() - nightlightStartTime)/((float)nightlightDelayMs);
    if (nightlightFade)
    {
      bri = briNlT + ((nightlightTargetBri - briNlT)*nper);
      if (nightlightColorFade)                                                        // color fading only is enabled with "NF=2"
      {
        for (byte i=0; i<4; i++) col[i] = colNlT[i]+ ((colSec[i] - colNlT[i])*nper);  // fading from actual color to secondary color
      }
      colorUpdated(5);
    }
    if (nper >= 1)
    {
      nightlightActive = false;
      if (!nightlightFade)
      {
        bri = nightlightTargetBri;
        colorUpdated(5);
      }
      updateBlynk();
      if (bri == 0) briLast = briNlT;
    }
  } else if (nightlightActiveOld) //early de-init
  {
    nightlightActiveOld = false;
  }

  //also handle preset cycle here
  if (presetCyclingEnabled && (millis() - presetCycledTime > presetCycleTime))
  {
    applyPreset(presetCycCurr,presetApplyBri,presetApplyCol,presetApplyFx);
    presetCycCurr++; if (presetCycCurr > presetCycleMax) presetCycCurr = presetCycleMin;
    if (presetCycCurr > 25) presetCycCurr = 1;
    colorUpdated(8);
    presetCycledTime = millis();
  }
}
