#include "StabilityAI.h"

namespace Upp {

StabilityAI::StabilityAI()
{
	auto pem_filename = ConfigFile(AppendFileName("StabilityAI", "roots.pem"));
	if (FileExists(pem_filename)) {
		roots_pem = LoadFile(pem_filename).ToStd();
	}
	
}

StabilityAI::~StabilityAI()
{
}

void StabilityAI::SetAPIKey(const String& api_key)
{
	apiSecret = api_key;
	UpdatedApiKey_impl();
}

StabilityAI::Result StabilityAI::GenerateImages(const InputParameters& imageParams, const Vector<ApiInputFile>& files, ProgressObject progressObj)
{
	Result results;
	results.error = !GenerateImages_impl(imageParams, files, results, progressObj);
	
	if (results.error)
		progressObj.Error("Error");
	else
		progressObj.Update("Done receiving", 100, 100);
	
	return pick(results);
}

} // Upp
