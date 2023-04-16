#include "ReplicateAI.h"

#define USER_AGENT	"TL_AiPainter"

namespace Upp {

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace {

const int REPLICATE_MAX_DOWNLOAD = 1024 * 1024 * 1024; // 1024 MB
const int REPLICATE_MAX_SCHEMA_SIZE = 4 * 1024 * 1024; // 4 MB

} // anon

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------


Replicate::Replicate()
{
	apiSecret = String::GetVoid();
}

void Replicate::SetSecret(const char *secret)
{
	apiSecret = secret;
}

Replicate::Prediction Replicate::Predict(const String& version, Value input, const Vector<ApiInputFile>& files, ProgressObject& progressObj, ValueMap outputModel)
{
	Prediction prediction;
	prediction.success = false;
	
	if (apiSecret.IsVoid()) {
		LOG("replicate api secret not set");
		return prediction;
	}
	
	LOG("Replicate Prediction request");
	LOG("Version: " << version);
	
	String predUrl = "https://api.replicate.com/v1/predictions";
	
	ApiHttpRequest request;
	request.Url(predUrl);
	request.Authorization(Format("Token %s", apiSecret));
	request.SetMultipart(false);
	request.MaxContentSize(REPLICATE_MAX_DOWNLOAD);
	request.KeepAlive();
	
	request.AddParameters(input);
	request.AddFiles(files);
	
	// wrap it
	ValueMap m;
	m("version", version)("input", request.GetInput());
	
	request.ApplyInput(m);
	
	progressObj.Update("Requesting", 0, 100);
	String body = request.Execute(progressObj);
	
	if (!request.IsSuccess() || body.IsVoid()) {
		LOG("Request failed: " << request.GetErrorDesc());
		DUMP(request.GetContent());
		prediction.error_message = Format("request failed %d", request.GetStatusCode());
		return prediction;
	}
	
	LOG("Response received");
	prediction.responseRaw = body;
	//DUMP(prediction.responseRaw);
	
	progressObj.Update("Requesting", 10, 100);
	
	auto j = ParseJSON(body);
	if (!j["error"].IsNull()) {
		LOG("Response = error");
		DUMP(j["error"]);
		prediction.error_message = j["error"];
		return prediction;
	}
	
	// grab the id
	prediction.id = j["id"];
	
	// build url
	String pollUrl;
	pollUrl << "https://api.replicate.com/v1/predictions/" << prediction.id;
	
	Value jj;
	Value output;

	// set up the poll request
	auto& poll = request;

	poll.ClearPost();
	poll.ClearContent();
	poll.Url(pollUrl);

	poll.GET();
	
	for (;;) {
		// no rush - predictions take time!
		// also max 10/s should be fine!
		Thread::Sleep(300);
		
		LOG("Polling: " << pollUrl);
		String resp = poll.Execute(Null); // we don't want all the http stuff for the polls
		if (poll.IsError()) {
			LOG("Request failed: " << poll.GetErrorDesc());
			DUMP(poll.GetContent());
			progressObj.Update("error", 100, 100);
			return prediction;
		}
		
		LOG("Poll response:");
		prediction.responseRaw = resp;
		DUMP(resp);
			
		jj = ParseJSON(resp);
		
		if (!jj["error"].IsNull()) {
			LOG("Response = error");
			DUMP(j["error"]);
			progressObj.Update("error", 100, 100);
			return prediction;
		}
		
		// this is some special code to try and parse the log for a progress percentage
		// grab the last line of the log for progress
		String progressMessage = jj["status"];
		String requestLog = jj["logs"];
		DUMP(requestLog);
		int prog = 0;
		if (!requestLog.IsEmpty()) {
			int b = requestLog.ReverseFind("%");
			if (b > 0) {
				int a = b - 1;
				while (a >= 0 && (b-a) < 3 && IsDigit(requestLog[a])) {
					--a;
				}
				if (b-a > 1) {
					auto pct = requestLog.Mid(a, b-a);
					prog = ScanInt(pct);
				}
			}
		}
		progressObj.Log(jj["logs"]);
		//LOG("Assigned LogValue:");
		//DUMP(progressObj.LogValue);
		if (prog)
			progressObj.Update(progressMessage, prog, 100);
		else
			progressObj.Update(progressMessage, 0,0);
		
		// check if output has been added
		output = jj["output"];
		if (!output.IsNull())
			break;
	}
	
	DUMP(prediction.responseRaw);
	
	Vector<String> urls;
	Vector<String> texts;
	
	// we need to parse the Output schema of the model to determine how to handle the output
	auto& output_type = outputModel["type"];
	
	if (output_type == "string") {
		if (outputModel["format"] == "uri") {
			urls << output;
		}
		else {
			texts << output;
		}
	}
	else if (output_type == "array") {
		auto& items = outputModel["items"];
		if (items["type"] == "string") {
			if (items["format"] == "uri") {
				for (auto& o : output)
					urls << o;
			}
			else {
				for (auto& o : output)
					texts << o;
			}
		}
	}
	
	// download all urls
	int i = 0, N = urls.GetCount();
	for (auto& url : urls) {
		progressObj.Update(Format("downloading %d/%d", i+1, N), i, N);
		
		String file = DownloadRetry(url, progressObj);
		if (file.IsEmpty())
			continue;
		
		LOG("download complete - adding to binaries");
		
		Output out;
		out.data = pick(file);
		out.type = OUTPUT_IMAGE;
		prediction.outputs.Add(pick(out));
	}
	
	// add all texts
	for (auto& txt : texts) {
		LOG("adding text output: " << txt);
		Output out;
		out.data = txt;
		out.type = OUTPUT_STRING;
		prediction.outputs.Add(pick(out));
	}
	
	progressObj.Update("done", 100,100);
	
	LOG("Request successful");
	prediction.success = true;
	return pick(prediction);
}

String Replicate::DownloadAuth(const String& url, ProgressObject progressObj)
{
	LOG("Downloading: " << url);
	
	ApiHttpRequest request;
	request.Url(url);
	request.Authorization(Format("Token %s", apiSecret));
	request.SetMultipart(false);
	request.MaxContentSize(REPLICATE_MAX_SCHEMA_SIZE);
	
	String body = request.Execute(progressObj);
	if (request.IsError() || body.IsVoid()) {
		LOG("download failed ; code = " << request.GetStatusCode() << " desc = " << request.GetErrorDesc());
		DUMP(body);
		return String::GetVoid();
	}
	
	LOG("downloaded " << body.GetCount() << " bytes");
	return body;
}

#if 0
//void Replicate::DreamBooth(const String& zipFileName, const String& instancePrompt, const String& classPrompt, int maxTrainSteps)
String Replicate::DreamBooth(const String& fullname, const ValueMap& input)
{
	/*
	// request an upload slot
	LOG("DreamBooth");
	String url = "https://dreambooth-api-experimental.replicate.com/v1/upload/data.zip";
	LOG("Requesting upload: " << url);
	
	ReplicateRequest request(url, apiSecret);
	String body = request.Execute();
	ValueMap response = ParseJSON(body);
	if (IsError(response)) {
		LOG("Abort! Response JSON invalid");
		DUMP(body);
		return;
	}
	DUMP(AsJSON(response, true));
	
	// now we have the urls
	String uploadUrl = response["upload_url"];
	String servingUrl = response["serving_url"];
	
	// load the zip file
	String buffer = LoadFile(zipFileName);
	if (buffer.IsVoid()) {
		LOG("Abort! Failed to load the training material .zip file.");
		return;
	}
	// upload the zip
	LOG("Uploading zip file");
	HttpRequest upload(uploadUrl);
	request.Header("Content-Type", "application/zip");
	request.Header("Content-Length", Format64(buffer.GetCount()));
	request.Post(buffer);
	body = request.Execute();
	if (request.IsError()) {
		LOG("Abort! Upload failed: " << upload.GetErrorDesc());
		DUMP(body);
		return;
	}
	buffer.Clear(); // free that ram
	
	// now start the training job
	ValueMap input;
	input.Set("instance_prompt", instancePrompt);
	input.Set("class_prompt", classPrompt);
	input.Set("instance_data", servingUrl);
	input.Set("max_train_steps", maxTrainSteps);
	*/
	
	ValueMap params;
	params.Set("input", input);
	params.Set("model", fullname);
	//params.Set("webhook_completed", "");
	
	// send it
	LOG("Start training job");
	ReplicateRequest trainRequest("https://dreambooth-api-experimental.replicate.com/v1/trainings", apiSecret);
	trainRequest.PostJson(params);
	String body = trainRequest.Execute();
	if (trainRequest.IsError()) {
		LOG("Abort! Failed to start trainings: " << trainRequest.GetErrorDesc());
		DUMP(body);
		return String::GetVoid();
	}
	
	// the result is the dreambooth descriptor
	ValueMap trainObj = ParseJSON(body);
	if (IsError(trainObj)) {
		LOG("Abort! Response JSON for trainings is invalid");
		DUMP(body);
		return String::GetVoid();
	}
	
	
	// now we can start polling the status
	String pollUrl;
	pollUrl << "https://dreambooth-api-experimental.replicate.com/v1/trainings/" << trainObj["id"];
	
	ValueMap jj;
	String status;
	
	for (;;) {
		// they say that it takes about 20 min to train a dreambooth, so lets
		// sleep for 3 seconds between polls
		Thread::Sleep(3000);
		
		ReplicateRequest poll(pollUrl, apiSecret);
		poll.GET();
		
		LOG("Polling: " << pollUrl);
		String resp = poll.Execute();
		if (poll.IsError()) {
			LOG("Abort! Request failed: " << poll.GetErrorDesc());
			return String::GetVoid();
		}
		
		LOG("Poll response:");
		DUMP(resp);
			
		jj = ParseJSON(resp);
		if (IsError(jj)) {
			LOG("Abort! Poll response JSON is invalid");
			DUMP(body);
			return String::GetVoid();
		}
		
		// check if it succeeded
		status = jj["status"];
		if (status == "succeeded" || status == "error")
			break;
	}
	
	LOG("Stopped polling");
	DUMP(status);
	
	if (status == "error")
		return String::GetVoid();
	
	// now you have your model
	String version = jj["version"];
	DUMP(version);
	
	//
	LOG("jj is the complete response");
	DUMP(jj);
	
	return version;
}
#endif

void Replicate::Start(ApiHttpRequest& request)
{
	LOG("Opening Replicate connection");
	
	request.Authorization(Format("Token %s", apiSecret));
	request.SetMultipart(false);
	request.MaxContentSize(REPLICATE_MAX_SCHEMA_SIZE);
	request.KeepAlive();
}

String Replicate::Download(ApiHttpRequest& request, const String& url)
{
	LOG("Downloading: " << url);
	
	request.Url(url);
	
	String body = request.Execute();
	if (request.IsError() || body.IsVoid()) {
		LOG("download failed ; code = " << request.GetStatusCode() << " desc = " << request.GetErrorDesc());
		DUMP(body);
		return String::GetVoid();
	}
	
	LOG("downloaded " << body.GetCount() << " bytes");
	return pick(body);
}

bool Replicate::End(ApiHttpRequest& request)
{
	bool error = request.IsError();
	request.Close();
	return error;
}

} // Upp

