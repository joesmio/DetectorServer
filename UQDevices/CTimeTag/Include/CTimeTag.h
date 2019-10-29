#ifndef CTIMETAG_ONCE
#define CTIMETAG_ONCE

#define _CRT_SECURE_NO_DEPRECATE  //supress microsoft warnings
#include <string>  




//using namespace std;

typedef long long TimeType;
typedef unsigned char ChannelType;


namespace TimeTag
{

class CUsb;
class CHelper;
class CTimetagReader;
class CLogic;
class CTest;

			

		class Exception 
	{
	private:
		std::string text;
	public:
		Exception(const char * m):
			text(m){}  
	Exception(std::string m):
			text(m)
		{
		}

		std::string GetMessageText()
		{
			return text;
		}

	};
			

class CTimeTag
{
private:
   		CUsb *usb;
		CHelper *helper;
		CTimetagReader *reader;
		CLogic *logic;
		CTest *test;
		double caldata[16];
		double resolution;
		
		 
public:

		CTimeTag();
		~CTimeTag();

		CTimetagReader * GetReader();
		CLogic * GetLogic();

		void Open(int nr= 1);
		bool IsOpen();
		void Close();
		void Calibrate();
        void SetInputThreshold(int input, double voltage);
        void SetFilterMinCount(int MinCount);

        void SetFilterMaxTime(int MaxTime);
		 
        double GetResolution()  ;
        int GetFpgaVersion()  ;
        void SetLedBrightness(int percent) ;
        std::string GetErrorText(int flags); 
        void EnableGating(bool enable);
        void GatingLevelMode(bool enable);
        void SetGateWidth(int duration);
		void SwitchSoftwareGate(bool onOff);
        


        void SetInversionMask(int mask);
        void SetDelay(int Input, int Delay);
        int ReadErrorFlags();
        int GetNoInputs();
        void UseTimetagGate(bool use);
        void UseLevelGate(bool p);
        bool LevelGateActive();
        void Use10MHz(bool use);  
        void SetFilterException(int exception);
		CTest * GetTest();
		CHelper * GetHelper();

		void StartTimetags();
		void StopTimetags();
		int ReadTags(ChannelType*& channel_ret, TimeType *&time_ret);
		void SaveDcCalibration(char * filename);
		void LoadDcCalibration(char * filename);
		void SetFG(int periode, int high);

private:
//		void CheckOpen();
		void Init();
		double GetDcCalibration(int chan);


};
}

#endif
