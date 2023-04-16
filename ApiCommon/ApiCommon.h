#ifndef _ApiCommon_ApiCommon_h
#define _ApiCommon_ApiCommon_h

#include <Core/Core.h>

namespace Upp
{

struct ProgressObject : Moveable<ProgressObject>
{
	Event<const String&, int, int> Update;
	Event<const String&> Error;
	Event<const String&> Success;
	Event<const Value&> Log;
	
	ProgressObject() = default;
	ProgressObject(const Nuller&) {
		Update = Null;
		Error = Null;
		Success = Null;
		Log = Null;
	}
	
	bool IsNullInstance() const {
		return !(Update || Error || Success || Log);
	}
};

struct ApiPrompt : Moveable<ApiPrompt> {
	String prompt;
	double weight;
	ApiPrompt() = default;
	ApiPrompt(const String& s, double w = DOUBLE_NULL) : prompt(s), weight(w) {}
};
typedef Vector<ApiPrompt> ApiPromptSet;

struct ApiInputFile : Moveable<ApiInputFile> {
	String name;
	String mime;
	String filename;
	String data;
	
	//ApiInputFile() = default;
	//rval_default(ApiInputFile);
};

struct ApiInputSet : Pte<ApiInputSet> {
	Vector<ApiInputFile> files;
	ValueMap params;
	ProgressObject progress;
	Vector<ApiPromptSet> prompt_sets;
	bool multipart_form = false;
	
	ApiInputSet() = default;
};

class ApiHttpRequest : public HttpRequest
{
public:
	ApiHttpRequest();
	ApiHttpRequest(const String& url);
	
	void SetMultipart(bool multi);
	
	void AddParameters(const ValueMap& map);
	void AddFiles(const Vector<ApiInputFile>& files);
	
	ValueMap GetInput() { return json_input; }
	void SetInput(ValueMap map) { json_input = map; }
	void ApplyInput();
	void ApplyInput(ValueMap map);
	
	String Execute();
	String Execute(ProgressObject progress);
	void ExecuteStream(Event<const void *, int> streamEvent);

protected:
	ValueMap json_input;

private:
	bool is_multipart;
	
};

String DownloadFile(const String& url, ProgressObject progress);
String DownloadRetry(const String& url, ProgressObject progress, int max_tries = 3);

} // Upp

#endif
