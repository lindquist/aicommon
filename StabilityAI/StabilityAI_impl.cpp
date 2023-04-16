#define WIN32_LEAN_AND_MEAN

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <google/protobuf/message_lite.h>

#undef ERROR // windows thought it a good idea to have a *macro* called "ERROR" ...
#include "stabilityai/generation.grpc.pb.h"

// this hack is needed or compiling failed in Core headers
#define timeGetTime timeGetTime__hack
inline int timeGetTime__hack() { return 0; }

// get the stuff
#include "StabilityAI.h"

namespace {

// helper to convert from the app property form to the actual enums used by the api
gooseai::DiffusionSampler FindSampler(const char* s) {
	using gooseai::DiffusionSampler;
	if (stricmp("ddim", s) == 0) { return DiffusionSampler::SAMPLER_DDIM; }
	else if (stricmp("ddpm", s) == 0) { return DiffusionSampler::SAMPLER_DDPM; }
	else if (stricmp("k_euler", s) == 0) { return DiffusionSampler::SAMPLER_K_EULER; }
	else if (stricmp("k_euler_ancestral", s) == 0) { return DiffusionSampler::SAMPLER_K_EULER_ANCESTRAL; }
	else if (stricmp("k_heun", s) == 0) { return DiffusionSampler::SAMPLER_K_HEUN; }
	else if (stricmp("k_dpm_2", s) == 0) { return DiffusionSampler::SAMPLER_K_DPM_2; }
	else if (stricmp("k_dpm_2_ancestral", s) == 0) { return DiffusionSampler::SAMPLER_K_DPM_2_ANCESTRAL; }
	else if (stricmp("k_lms", s) == 0) { return DiffusionSampler::SAMPLER_K_LMS; }
	else if (stricmp("k_dpmpp_2s_ancestral", s) == 0) { return DiffusionSampler::SAMPLER_K_DPMPP_2S_ANCESTRAL; }
	else if (stricmp("k_dpmpp_2m", s) == 0) { return DiffusionSampler::SAMPLER_K_DPMPP_2M; }
	else if (stricmp("k_dpmpp_sde", s) == 0) { return DiffusionSampler::SAMPLER_K_DPMPP_SDE; }
	// fixme log an error or something
	else return DiffusionSampler::SAMPLER_K_DPMPP_2M;
}

gooseai::GuidancePreset FindGuidancePreset(const char* s) {
	using gooseai::GuidancePreset;
	if (stricmp("NONE", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_NONE; }
	else if (stricmp("SIMPLE", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_SIMPLE; }
	else if (stricmp("FAST_BLUE", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_FAST_BLUE; }
	else if (stricmp("FAST_GREEN", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_FAST_GREEN; }
	else if (stricmp("SLOW", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_SLOW; }
	else if (stricmp("SLOWER", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_SLOWER; }
	else if (stricmp("SLOWEST", s) == 0) { return GuidancePreset::GUIDANCE_PRESET_SLOWEST; }
	// fixme log an error or something
	return GuidancePreset::GUIDANCE_PRESET_NONE;
}

void LogFunc(gpr_log_func_args* args) {
	LOG(Upp::Format("(%d)%s:%d: %s", args->severity, args->file, args->line, args->message));
}

void ShutdownStability() {
	google::protobuf::ShutdownProtobufLibrary();
}

// try to fix memory leaks by shutting down protobuf properly
EXITBLOCK
{
	ShutdownStability();
};

} // anon

namespace Upp {
	
void StabilityAI::UpdatedApiKey_impl()
{
	// fixme try and keep the channel open instead of creating a new one for each request
	//		 are channels thread safe?
}
	
// this is the implementation method, isolated in this module to keep grpc out of the general
// namespace
bool StabilityAI::GenerateImages_impl(const InputParameters& input, const Vector<ApiInputFile>& files, Result& out_result, ProgressObject progressObj)
{
	if (apiSecret.IsEmpty()) {
		out_result.error = true;
		out_result.error_message = "missing api key";
		RLOG("cannot make stability request - missing api key");
		return false;
	}
	if (roots_pem.empty()) {
		out_result.error = true;
		out_result.error_message = "missing grpc root certificates";
		return false;
	}
	
	gpr_set_log_verbosity(GPR_LOG_SEVERITY_INFO);
	gpr_set_log_function(LogFunc);
	
	LOG("Starting Stability AI gRPC request");
	
	const char* endpoint = "grpc.stability.ai:443";
	
	// a few helpers
	auto HasParam = [&](const char* key) {
		return (input.params.Find(key) >= 0);
	};
	auto IntParam = [&](const char* key) {
		return (int)input.params[key];
	};
	auto FloatParam = [&](const char* key) {
		return (float)input.params[key];
	};
	auto StringParam = [&](const char* key) {
		static Upp::String temp_str;
		temp_str = input.params[key];
		return ~temp_str;
	};
	
	// set up the channel
	grpc::ClientContext context;
	//auto credentials = grpc::InsecureChannelCredentials();
	auto apiKeyCredentials = grpc::AccessTokenCredentials(~apiSecret);
	auto sslCredentialOptions = grpc::SslCredentialsOptions();
	sslCredentialOptions.pem_root_certs = roots_pem;
	//sslCredentialOptions.pem_root_certs = // done with env var
	auto sslCredentials = grpc::SslCredentials(sslCredentialOptions);
	auto channelCredentials = grpc::CompositeChannelCredentials(sslCredentials, apiKeyCredentials);
	auto channel = grpc::CreateChannel(endpoint, channelCredentials);
	//if (channel->GetState(true) !=
	
	// create GenerationService
	auto stub = gooseai::GenerationService::NewStub(channel);
	
	// basically try to translate the example from:
	// https://platform.stability.ai/docs/getting-started/typescript-client
	// also, the python client was very helpful
	// https://github.com/Stability-AI/stability-sdk/blob/main/src/stability_sdk/client.py
	// finally, the grpc++ tutorials have some good info:
	// https://grpc.io/docs/languages/cpp/basics/
	
	// setup up image parameters
	auto imageParams = new gooseai::ImageParameters;
	
	int maxImages = std::min(input.num_images, 10);
	imageParams->set_samples(maxImages);
	for (int i = 0; i < maxImages; ++i) {
		imageParams->add_seed(input.seed[i]);
	}
	
	if (HasParam("width"))
		imageParams->set_width(IntParam("width"));
	if (HasParam("height"))
		imageParams->set_height(IntParam("height"));
	
	if (HasParam("steps"))
		imageParams->set_steps(IntParam("steps"));
	
	if (HasParam("sampler")) {
		auto transformType = new gooseai::TransformType;
		transformType->set_diffusion(FindSampler(StringParam("sampler")));
		imageParams->set_allocated_transform(transformType);
	}
	
	
	// step parameter
	auto stepParam = imageParams->add_parameters();
	stepParam->set_scaled_step(0); // ?
	
	if (HasParam("cfg_scale")) {
		auto samplerParams = new gooseai::SamplerParameters;
		samplerParams->set_cfg_scale(FloatParam("cfg_scale"));
		stepParam->set_allocated_sampler(samplerParams);
	}
	
	bool has_start_sched = HasParam("start_schedule");
	bool has_end_sched = HasParam("end_schedule");
	if (has_start_sched || has_end_sched) {
		auto scheduleParams = new gooseai::ScheduleParameters;
		if (has_start_sched)
			scheduleParams->set_start(FloatParam("start_schedule"));
		if (has_end_sched)
			scheduleParams->set_end(FloatParam("end_schedule"));
		stepParam->set_allocated_schedule(scheduleParams);
	}
	
	if (HasParam("guidance_preset")) {
		auto guidanceParams = new gooseai::GuidanceParameters;
		guidanceParams->set_guidance_preset(FindGuidancePreset(StringParam("guidance_preset")));
		stepParam->set_allocated_guidance(guidanceParams);
	}
	
	LOG("engine id: " << input.engine_id);
	
	// set up the request
	gooseai::Request request;
	request.set_engine_id(input.engine_id);
	request.set_requested_type(gooseai::ArtifactType::ARTIFACT_IMAGE);
	request.set_allocated_image(imageParams);
	
	// add prompts
	for (auto& p : input.prompts) {
		ASSERT(!p.prompt.IsEmpty());
		LOG("Adding prompt: '" << p.prompt << "'" << " weight = " << p.weight);
		
		auto promptText = request.add_prompt();
		promptText->set_text(~p.prompt);
		
		if (p.weight != Upp::DOUBLE_NULL) {
			auto prompt_params = new gooseai::PromptParameters;
			prompt_params->set_weight(float(p.weight));
			promptText->set_allocated_parameters(prompt_params);
		}
	}
	
	// add images
	int img_id = 0;
	for (auto& file : files) {
		LOG("Adding image: " << file.name);
		
		auto img_prompt = request.add_prompt();
		auto img_artifact = new gooseai::Artifact;
		img_artifact->set_id(img_id++);
		img_artifact->set_mime(~file.mime);
		//img_artifact->set_magic("PNG");
		if (file.name == "init_image") {
			img_artifact->set_type(gooseai::ArtifactType::ARTIFACT_IMAGE);
			auto prompt_params = new gooseai::PromptParameters;
			prompt_params->set_init(true);
			img_prompt->set_allocated_parameters(prompt_params);
		}
		else if (file.name == "mask_image") {
			img_artifact->set_type(gooseai::ArtifactType::ARTIFACT_MASK);
		}
		img_artifact->set_size(file.data.GetCount());
		img_artifact->set_binary(file.data.ToStd());
		img_artifact->set_index(0);
		
		img_prompt->set_allocated_artifact(img_artifact);
	}
	
	// request "done"
	//DUMP(request.DebugString());
	
	int progress = 0;
	int progressTotal = maxImages;
	progressObj.Update("GenerationService ...", progress, progressTotal);
	
	auto answers = stub->Generate(&context, request);
	
	progressObj.Update("Receiving images ...", progress, progressTotal);

	gooseai::Answer answer;
	
	int imageNo = 1;
	uint64_t answer_id = 0;
	
	out_result.uid = Null;
	out_result.request_id = Null;
	out_result.created = UINT64_MAX;
	out_result.received = UINT64_MAX;
	
	while (answers->Read(&answer)) {
		auto n = answer.artifacts_size();
		LOG("Answer received: " << n << " artifacts");
		//DUMP(answer.DebugString());
		
		for (int i = 0; i < n; ++i) {
			LOG("Processing artifact " << i);

			// grab output from Answer
			
			// use some details from the first artifact
			if (out_result.created == UINT64_MAX) {
				//out_result.uid = answer.answer_id();
				out_result.request_id = answer.request_id();
				out_result.received = answer.received();
				out_result.created = answer.created();
			}
			
			auto& artifact = answer.artifacts(i);
			
			auto atype = artifact.type();
			if (atype == gooseai::ArtifactType::ARTIFACT_IMAGE) {
				LOG("Got image: " << artifact.mime());
				if (artifact.mime() == "image/png") {
					// grab output from Artifact
					
					// the binary
					out_result.binaries.Add(artifact.binary());
					out_result.seeds.Add(artifact.seed());
					
					// grab the uuid from the first artifact
					if (IsNull(out_result.uid)) {
						//output.finish_reason = (int)artifact.finish_reason();
						out_result.uid = artifact.uuid();
					}

					progressObj.Update("Receiving", ++progress, progressTotal);
					
					++imageNo;
				}
				else {
					LOG("unsupported mime type: " << artifact.mime());
				}
			}
			else if (atype == gooseai::ArtifactType::ARTIFACT_CLASSIFICATIONS) {
				LOG("Got classifications");
				// save the string ?
				//DUMP(artifact.DebugString());
			}
			else {
				LOG("Unsupported artifact type: " << (int)atype);
				// unknown artifact type
				//LOG("unknown artifact type: " << (int)atype);
			}
		}
	}
	
	auto status = answers->Finish();
	if (!status.ok()) {
		out_result.error = true;
		out_result.error_message = status.error_message().c_str();
		DUMP(status.error_message().c_str());
		return false;
	}
	
	request.Clear();

	//DUMP(context.debug_error_string());
	
	//DUMP((int)channel->GetState(true));
	
	progressObj.Update("Done receiving", 100, 100);

	return true;
}

} // Upp
