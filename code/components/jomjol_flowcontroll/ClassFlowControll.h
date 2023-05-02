#pragma once

#ifndef CLASSFLOWCONTROLL_H
#define CLASSFLOWCONTROLL_H

#include <string>

#include "ClassFlow.h"
#include "ClassFlowTakeImage.h"
#include "ClassFlowAlignment.h"
#include "ClassFlowCNNGeneral.h"
#include "ClassFlowPostProcessing.h"
#ifdef ENABLE_MQTT
	#include "ClassFlowMQTT.h"
#endif //ENABLE_MQTT
#ifdef ENABLE_INFLUXDB
	#include "ClassFlowInfluxDB.h"
	#include "ClassFlowInfluxDBv2.h"
#endif //ENABLE_INFLUXDB
#include "ClassFlowCNNGeneral.h"


class ClassFlowControll : public ClassFlow
{
protected:
	std::vector<ClassFlow*> FlowControll;
	std::vector<ClassFlow*> FlowControlPublish;
	std::vector<strFlowState*> FlowStateErrorsEvaluation;
	std::vector<strFlowState*> FlowStateErrorsPublish;

	ClassFlowTakeImage* flowtakeimage;
	ClassFlowAlignment* flowalignment;	
	ClassFlowCNNGeneral* flowanalog;
	ClassFlowCNNGeneral* flowdigit;
	ClassFlowPostProcessing* flowpostprocessing;
	ClassFlowMQTT* flowMQTT;
	ClassFlowInfluxDB* flowInfluxDB;
	ClassFlowInfluxDBv2* flowInfluxDBv2;
	
	ClassFlow* CreateClassFlow(std::string _type);
	void SetInitialParameter(void);	

	float AutoInterval;
	bool AutoStart;
	bool SetupModeActive;
	bool readParameterDone;
	
	bool aktflowerror;
	std::string aktstatus;
	std::string aktstatusWithTime;

public:
	ClassFlowControll();
	virtual ~ClassFlowControll();
	bool InitFlow(std::string config);
	void DeinitFlow(void);
	bool doFlowImageEvaluation(string time);
	bool doFlowPublishData(string time);
	bool doFlowTakeImageOnly(string time);
	bool getStatusSetupModus(){return SetupModeActive;};
	string getReadout(bool _rawvalue, bool _noerror, int _number);
	string getReadoutAll(int _type);	
	bool UpdatePrevalue(std::string _newvalue, std::string _numbers, bool _extern);
	string GetPrevalue(std::string _number = "");	
	bool ReadParameter(FILE* pfile, string& aktparamgraph);	
	string getJSON();
	string getNumbersName();

	string TranslateAktstatus(std::string _input);

	#ifdef ENABLE_MQTT
	bool StartMQTTService();
	#endif //ENABLE_MQTT

	void DigitalDrawROI(CImageBasis *_zw);
	void AnalogDrawROI(CImageBasis *_zw);

	esp_err_t GetJPGStream(std::string _fn, httpd_req_t *req);
	esp_err_t SendRawJPG(httpd_req_t *req);

	std::string doSingleStep(std::string _stepname, std::string _host);

	bool isAutoStart();
	bool isAutoStart(long &_interval);

	std::string getActStatusWithTime();
	std::string getActStatus();
	void setActStatus(std::string _aktstatus);
	void setActFlowError(bool _aktflowerror);
	bool getActFlowError();
	bool FlowStateErrorsOccured();
	void AutomaticFlowErrorHandler();

	std::vector<HTMLInfo*> GetAllDigital();
	std::vector<HTMLInfo*> GetAllAnalog();	

	t_CNNType GetTypeDigital();
	t_CNNType GetTypeAnalog();

	int CleanTempFolder();

	string name(){return "ClassFlowControll";};
};

#endif //CLASSFLOWCONTROLL_H



