#ifndef _OpenAI_OpenAI_h
#define _OpenAI_OpenAI_h

#include <Core/Core.h>
#include <ApiCommon/ApiCommon.h>

namespace Upp {

class OpenAI {
	
public:
	typedef OpenAI CLASSNAME;
	
	OpenAI();
	
	void SetSecret(const char* secret);
	bool CheckSecret(String& message);
	
	Value GetModels() const { return models; }
	
	// returns response with inlined binaries removed
	Value ImagesRequest(const String& absPath, bool multipart, Vector<String>& outputs, ValueMap params, const Vector<ApiInputFile>& files, ProgressObject progress);
	
	// completions endpoint
	Value CreateCompletion(Value input, Event<Value> stream);
	Value CreateCompletion(Value input);
	
	Value ChatCompletion(Value input);

private:
	String apiSecret;
	Value models;
};

} // Upp

#endif
