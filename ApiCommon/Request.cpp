#include "ApiCommon.h"

#define USER_AGENT	"UppOracle"

namespace Upp {
	
namespace {
	
const int MAX_DOWNLOAD = 1024 * 1024 * 1024; // 1024 MB

String EncodeDataUri(const String& mime, const String& data) {
	String dataUri;
	dataUri << "data:" << mime << ";base64," << Base64Encode(data);
	return dataUri;
}

} // anon

ApiHttpRequest::ApiHttpRequest()
{
	UserAgent(USER_AGENT);
	ChunkSize(8192);
	
	is_multipart = false;
}

ApiHttpRequest::ApiHttpRequest(const String& url)
: ApiHttpRequest()
{
	Url(url);
}

void ApiHttpRequest::SetMultipart(bool multi)
{
	is_multipart = multi;
}

void ApiHttpRequest::AddParameters(const ValueMap& params)
{
	// add the parameters
	for (int i = 0; i < params.GetCount(); ++i) {
		String key = params.GetKey(i);
		Value v = params.GetValue(i);
		if (is_multipart) {
			String s;
			s << v;
			Part(key, s);
		}
		else
			json_input.Set(key, v);
	}
}

void ApiHttpRequest::AddFiles(const Vector<ApiInputFile>& files)
{
	// add the files
	for (int i = 0; i < files.GetCount(); ++i) {
		auto& file = files[i];
		if (is_multipart)
			Part(file.name, file.data, file.mime, file.filename);
		else
			json_input.Set(file.name, EncodeDataUri(file.mime, file.data));
	}
}

void ApiHttpRequest::ApplyInput()
{
	if (!is_multipart) {
		ContentType("application/json");
		Post(AsJSON(json_input));
	}
}

void ApiHttpRequest::ApplyInput(ValueMap map)
{
	if (!is_multipart) {
		ContentType("application/json");
		Post(AsJSON(map));
	}
}

String ApiHttpRequest::Execute()
{
	New(); // make fresh request - keep settings
	
	// content callback
	String body;
	WhenContent = [&](const void *ptr, int size) {
		body.Cat((const char *)ptr, size);
	};
	
	while(Do()) {
		// no status updates to do here
	}
	
	//return GetContent();
	return pick(body);
}

String ApiHttpRequest::Execute(ProgressObject progress)
{
	New(); // make fresh request - keep settings
	
	// content callback
	String body;
	WhenContent = [&](const void *ptr, int size) {
		body.Cat((const char *)ptr, size);
	};
	
	while(Do()) {
		progress.Update(GetPhaseName(), body.GetCount(), (int)GetContentLength());
	}
	
	//return GetContent();
	return pick(body);
}

void ApiHttpRequest::ExecuteStream(Event<const void *, int> streamEvent)
{
	New(); // make fresh request - keep settings
	
	// content callback
	WhenContent = streamEvent;
	
	while(Do()) {
		// doing it
	}
}

//----------------------------------------------------------------------------

String DownloadFile(const String& url, ProgressObject progress)
{
	LOG("Downloading: " << url);
	progress.Update("downloading ...", 0,0);
	
	ApiHttpRequest request;
	request.Url(url);
	request.MaxContentSize(MAX_DOWNLOAD);
	request.GET();
	
	String body = request.Execute(progress);
	
	#if 0
	auto& header = request.GetHttpHeader();
	int N = header.fields.GetCount();
	for (int i = 0; i < N; ++i) {
		DUMP(header.fields.GetKey(i));
		DUMP(header.fields[i]);
	}
	#endif
	
	if (request.IsSuccess()) {
		LOG("downloaded " << body.GetCount() << " bytes");
		progress.Success("download complete");
		return pick(body);
	}
	else {
		LOG("download failed:");
		LOG("code = " << request.GetStatusCode());
		LOG("desc = " << request.GetErrorDesc());
		DUMP(body);
		DUMP(request.GetContent());
		progress.Error("download failed");
		return String::GetVoid();
	}
}

String DownloadRetry(const String& url, ProgressObject progress, int max_tries)
{
	String body;
	while (--max_tries > 0) {
		body = DownloadFile(url, progress);
		if (body.IsVoid()) {
			LOG("retrying ...");
			continue;
		}
		break;
	}
	return pick(body);
}

} // Upp
