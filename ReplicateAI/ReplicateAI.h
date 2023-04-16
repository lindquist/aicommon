#ifndef _ReplicateAI_ReplicateAI_h
#define _ReplicateAI_ReplicateAI_h

#include <Core/Core.h>
#include <ApiCommon/ApiCommon.h>

namespace Upp {

class Replicate {
public:
	enum {
		OUTPUT_IMAGE,
		OUTPUT_STRING
	};
	struct Output : Moveable<Output> {
		String data;
		int type;
	};
	struct Prediction : Moveable<Prediction> {
		String id;
		Vector<Output> outputs;
		String responseRaw;
		bool success;
		String error_message;
	};
	
public:
	typedef Replicate CLASSNAME;
	
	Replicate();
	
	void SetSecret(const char* secret);
	bool HasSecret() const { return !apiSecret.IsEmpty(); }
	
	Prediction Predict(const String& version, Value input, const Vector<ApiInputFile>& files, ProgressObject& progressObj, ValueMap outputModel);
	//String DreamBooth(const String& modelFullName, const ValueMap& input);

	String DownloadAuth(const String& url, ProgressObject progressObj);
	
	void Start(ApiHttpRequest& request);
	String Download(ApiHttpRequest& request, const String& url);
	bool End(ApiHttpRequest& request);

private:
	String apiSecret;
};

} // Upp

#endif
