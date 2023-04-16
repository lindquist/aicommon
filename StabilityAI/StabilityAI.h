#ifndef _StabilityAI_StabilityAI_h
#define _StabilityAI_StabilityAI_h

#include <vector>

#include <Core/Core.h>
#include <ApiCommon/ApiCommon.h>

namespace Upp {

class StabilityAI {
public:
	typedef Event<const String&,int,int> ProgressEvent;
	
	struct Result : Moveable<Result> {
		Vector<String> binaries;
		Vector<int64> seeds;
		
		String uid;
		String request_id;
		
		uint64 received;
		uint64 created;
		
		bool error;
		String error_message;
		
		Result() = default;
	};
	
	struct InputParameters {
		String engine_id;
		
		ApiPromptSet prompts;
		int64 seed[10];
		int num_images;
		
		ValueMap params;
	};
	
public:
	StabilityAI();
	~StabilityAI();
	
	void SetAPIKey(const String& api_key);
	
	// returns png files in strings
	Result GenerateImages(const InputParameters& imageParams, const Vector<ApiInputFile>& files, ProgressObject progressObj);

protected:
	// impl
	bool GenerateImages_impl(const InputParameters& input, const Vector<ApiInputFile>& files, Result& out_result, ProgressObject progressObj);
	void UpdatedApiKey_impl();
	
protected:
	String apiSecret;
	
	std::string roots_pem;
};

}

#endif
