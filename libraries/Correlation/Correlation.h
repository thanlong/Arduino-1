#pragma once
//
//    FILE: Correlation.h
//  AUTHOR: Rob Tillaart
// VERSION: 0.2.0
// PURPOSE: Calculate Correlation from a small dataset.
// HISTORY: See Correlation.cpp
//


#include "Arduino.h"


#define CORRELATION_LIB_VERSION          (F("0.2.0"))


class Correlation
{
public:
  Correlation(uint8_t size = 20);   // WARNING calculate memory usage !!
  ~Correlation();

  // returns true if the pair of values is added to internal array.
  // returns false when internal array is full.
  bool    add(float x, float y);

  // administrative functions
  uint8_t count() { return _count; };
  uint8_t size()  { return _size; };
  void    clear();


  // in running mode, adding new pair of values will replace old ones
  // this constantly adapts the regression parameters A and B (iff calculate is called)
  void    setRunningCorrelation(bool rc) { _runningMode = rc; };
  bool    getRunningCorrelation()        { return _runningMode; };


  // worker, to calculate the correlation parameters.
  // MUST be called before retrieving the parameters 
  //      A, B, R, Rsquare, Esquare, avgX and avgY
  //
  // parameter forced overrules the _needRecalculate flag.
  //           forced is default false to maintain backwards compatibility
  //
  // returns false if contains no elements ==> count() == 0
  bool    calculate(bool forced = false);
  // enables / disables R, Rsquare and Esquare calculation
  // This can be used to speed up the calculate function if
  // these values are not used in your project.
  void    setR2Calculation(bool doR2) { _doR2 = doR2; };
  bool    getR2Calculation() { return _doR2; };
  void    setE2Calculation(bool doE2) { _doE2 = doE2; };
  bool    getE2Calculation() { return _doE2; };


  // Y = A + B * X
  // note if no elements are added or calculate is not called
  //      the values for A and B are 0
  float   getA()       { return _a; };
  float   getB()       { return _b; };


  // getR() returns correlation coefficient  (0.2.0 fixed sign)
  float   getR()       { return _r; };
  float   getRsquare() { return _r * _r; };


  // returns sum of the errors squared == indication of 'spread'
  // the smaller this value the more the points are on/near one line.
  float   getEsquare() { return _sumErrorSquare; };


  // get the average values of the datasets (if count > 0)
  float   getAvgX()    { return _avgX; };
  float   getAvgY()    { return _avgY; };


  // based on the dataset get the estimated values for X and Y
  // it uses the last calculated A and B
  // library does not return a confidence interval for these values.
  float   getEstimateY(float x);
  float   getEstimateX(float y);


  // STATISTICAL
  float   getMinX();    // idem
  float   getMaxX();    // idem
  float   getMinY();    // idem
  float   getMaxY();    // idem


  // DEBUGGING - access to internal arrays.
  bool    setXY(uint8_t idx, float x, float y);  // returns true if succeeded
  bool    setX(uint8_t idx, float x);            // returns true if succeeded
  bool    setY(uint8_t idx, float y);            // returns true if succeeded
  float   getX(uint8_t idx);    // idem
  float   getY(uint8_t idx);    // idem

  float   getSumXiYi() { return _sumXiYi; };
  float   getSumXi2()  { return _sumXi2;  };
  float   getSumYi2()  { return _sumYi2;  };


private:
  uint8_t _size  = 0;
  uint8_t _idx   = 0;
  uint8_t _count = 0;
  bool    _runningMode = false;
  bool    _needRecalculate = true;
  bool    _doE2 = true;
  bool    _doR2 = true;

  float *  _x;
  float *  _y;

  float   _avgX;
  float   _avgY;
  float   _a;
  float   _b;
  float   _r;
  float   _sumErrorSquare;
  float   _sumXiYi;
  float   _sumXi2;
  float   _sumYi2;
};

// -- END OF FILE --
