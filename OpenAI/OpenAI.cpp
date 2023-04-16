#include "OpenAI.h"

namespace Upp {

namespace {
	
const int MAX_DOWNLOAD = 1024 * 1024 * 1024; // 1024 MB
const int MAX_UPLOAD = 5 * 1024 * 1024; // 5 MB

const char* COMPLETIONS_URL = "https://api.openai.com/v1/completions";
const char* CHAT_COMPLETIONS_URL = "https://api.openai.com/v1/chat/completions";

} // anon

OpenAI::OpenAI()
{
	apiSecret = String::GetVoid();
}

void OpenAI::SetSecret(const char *secret)
{
	apiSecret = secret;
}

bool OpenAI::CheckSecret(String& message)
{
	// make some request that requires authentication
	//HttpRequest::Trace();
	ApiHttpRequest request("https://api.openai.com/v1/models/davinci");
	request.Authorization(Format("Bearer %s", apiSecret));
	request.GET();
	String response = request.Execute();
	if (request.IsError()) {
		message = Format("error response = '%s' and request content = '%s'", response, request.GetContent());
		return false;
	}
	return !IsError(ParseJSON(response));
}

Value OpenAI::ImagesRequest(const String& absPath,
							bool multipart,
							Vector<String>& outputs,
							ValueMap params,
							const Vector<ApiInputFile>& files,
							ProgressObject progress)
{
	if (apiSecret.IsVoid()) {
		progress.Error("api key not set");
		return ErrorValue("api key not set");
	}
	
	progress.Update("generating ...", 0,0);
	
	String req_url = "https://api.openai.com/v1";
	req_url << absPath;
		
	ApiHttpRequest request(req_url);
	request.Authorization(Format("Bearer %s", apiSecret));
	request.MaxContentSize(MAX_DOWNLOAD);

	request.SetMultipart(multipart);
	
	request.AddParameters(params);
	request.AddFiles(files);
	
	// here we could get/set the input to wrap or something
	// but that's not necessary for openai
	request.ApplyInput();
	
	String body = request.Execute(progress);
	
	if (!request.IsSuccess()) {
		// body is empty here, but GetContent has the json response
		LOG("request failed code " << request.GetStatusCode());
		String json_string = request.GetContent();
		String msg;
		auto json = ParseJSON(json_string);
		if (IsValueMap(json))
			if (IsString(json["error"]["message"]))
				msg = json["error"]["message"];
		if (msg.IsEmpty())
			msg = Format("request failed %d", request.GetStatusCode());
		//progress.Log(AsJSON(json,true));
		progress.Error(msg);
		return ErrorValue(msg);
	}
	
	LOG("OpenAI request success");
	
	// decode each image from the response
	auto resp_ = ParseJSON(body);
	if (resp_.IsError()) {
		LOG("failed to parse json response");
		progress.Error("response json invalid");
		return ErrorValue("failed to parse json response");
	}
	
	ValueMap response = resp_;
	
	// if the files are "inlined" pick them out and sanitize returned response
	auto& response_format = params["response_format"];
	
	progress.Update("decoding response", 100,100);
	
	if (response_format == "b64_json") {
		for (auto& result : response["data"])
			outputs << pick(Base64Decode(result["b64_json"]));
		response.Set("data", Null); // too big to keep here - the files will be saved right after this return
		progress.Success("generations done");
		return response;
	}
	
	progress.Update("downloading artifacts", 100,100);
	
	//DUMP(body);

	// the files need to be downloaded
	for (auto& result : response["data"]) {
		auto& url = result["url"];
		String dl = DownloadRetry(url, progress);
		if (dl.IsVoid())
			LOG("failed to download output: " << url);
		else
			outputs << pick(dl);
	}
	// the urls will be invalid in 1hr, so just remove them ...
	response.Set("data", Null);
	
	progress.Success("generations done");
	
	return response;
}

Value OpenAI::CreateCompletion(Value input, Event<Value> stream)
{
	ApiHttpRequest request(COMPLETIONS_URL);
	request.Authorization(Format("Bearer %s", apiSecret));
	
	// let the user deal with the entire input for now
	request.SetMultipart(false);
	request.AddParameters(input);
	request.ApplyInput();
	
	ValueArray result;
	
	auto parse_event = [&](String body){
		DUMP(body);
		if (!body.StartsWith("data: ") || !body.EndsWith("\n\n"))
			return;
		int i = 6;
		int j = body.GetCount()-2;
		String event = body.Mid(i, j-i);
		if (event == "[DONE]") {
			stream(Null);
		}
		else {
			auto json = ParseJSON(event);
			result << json;
			stream(json);
		}
	};
	
	request.ExecuteStream([&](const void* data, int size){
		String s((const byte*)data, size);
		parse_event(pick(s));
	});
	
	if (request.IsError()) {
		String s;s << "error " << request.GetStatusCode() << " making request";
		return ErrorValue(s);
	}
	
	if (request.GetStatusCode() != 200) {
		auto json = ParseJSON(request.GetContent());
		if (!json.IsError()) {
			stream(json);
			result << json;
		}
	}
	
	return result;
}

Value OpenAI::CreateCompletion(Value input)
{
	ApiHttpRequest request(COMPLETIONS_URL);
	request.Authorization(Format("Bearer %s", apiSecret));
	
	// let the user deal with the entire input for now
	request.SetMultipart(false);
	request.AddParameters(input);
	request.ApplyInput();
	
	String body = request.Execute();
	
	LOG("--------------------------------------------------------");
	LOG("Requst complete:");
	DUMP(body);
	DUMP(request.GetContent());
	LOG("--------------------------------------------------------");
	
	if (request.IsError()) {
		ValueMap m;m
		("request", input)("response", body)
		("status_code", request.GetStatusCode())
		("error_desc", request.GetErrorDesc())
		("error_code", request.GetHttpHeader().fields.ToString())
		("content", request.GetContent());
		DUMP(m);
		return ErrorValue(AsJSON(m,true));
	}
	Value json = ParseJSON(body);
	return IsError(json) ? Value(body) : json;
}

Value OpenAI::ChatCompletion(Value input)
{
	ApiHttpRequest request(CHAT_COMPLETIONS_URL);
	request.Authorization(Format("Bearer %s", apiSecret));
	
	// let the user deal with the entire input for now
	request.SetMultipart(false);
	request.AddParameters(input);
	request.ApplyInput();
	
	String body = request.Execute();
	
	LOG("--------------------------------------------------------");
	LOG("Requst complete:");
	DUMP(body);
	DUMP(request.GetContent());
	LOG("--------------------------------------------------------");
	
	if (request.IsError()) {
		ValueMap m;m
		("request", input)("response", body)
		("status_code", request.GetStatusCode())
		("error_desc", request.GetErrorDesc())
		("error_code", request.GetHttpHeader().fields.ToString())
		("content", request.GetContent());
		DUMP(m);
		return ErrorValue(AsJSON(m,true));
	}
	Value json = ParseJSON(body);
	return IsError(json) ? Value(body) : json;
}


} // Upp
