#include <Geode/modify/MenuLayer.hpp>

#include <filesystem>
#include <chrono>

using namespace geode::prelude;

int getFrameCount();

class BackgroundUpdater : public CCLayer {
public:
	CCTextureCache* textureCache = CCTextureCache::sharedTextureCache();
	CCSprite* cSprite = nullptr;
	std::chrono::steady_clock::time_point startTimestamp = std::chrono::steady_clock::now();
	int frameCount = getFrameCount();
	int fps;
	
	void updateBackground(float) {
		if (!cSprite) {
			geode::log::warn("cSprite is not set!");
			return;
		}
		
		auto currentTimestamp = std::chrono::steady_clock::now();
		auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTimestamp - startTimestamp).count();
		auto frameIndex = (static_cast<int>((elapsedMilliseconds / (1000.0f / fps))) % frameCount) + 1;
		
		auto framesDir = Mod::get()->getSaveDir() / "menuVideoBgFrames";
		auto imagePath = framesDir / fmt::format("output_{:04d}.png", frameIndex);
		
		if (!std::filesystem::exists(imagePath)) {
			return;
		}
		
		auto texture = textureCache->addImage(imagePath.string().c_str(), true);
		
		if (!texture) {
			geode::log::warn("Failed to load texture at frame {}", frameIndex);
			return;
		}
		
		cSprite->setTexture(texture);
	}
	
	void startUpdating() {
		geode::log::info("Initializing scheduler (fps: {}, frameCount: {})", fps, frameCount);
		this->schedule(schedule_selector(BackgroundUpdater::updateBackground), 1.0f / 60);
		geode::log::info("Done!");
	}
};

int getFrameCount() {
	auto outDir = Mod::get()->getSaveDir() / "menuVideoBgFrames";
	auto count = 0;
	
	for (const auto& entry : std::filesystem::directory_iterator(outDir)) {
		if (entry.is_regular_file() && entry.path().extension() == ".png") {
			count++;
		}
	}
	
	return count;
}

bool ffmpegAvailable() {
	return std::system("ffmpeg -version >nul 2>&1") == 0;
}

void cleanFramesDir(std::filesystem::path framesDir, std::error_code errCode) {
	auto textureCache = CCTextureCache::sharedTextureCache();
	
	for (const auto& entry : std::filesystem::directory_iterator(framesDir)) {
		if (entry.is_regular_file() && entry.path().extension() == ".png") {
			textureCache->removeTextureForKey(entry.path().string().c_str());
		}
	}
	
	std::filesystem::remove_all(framesDir, errCode);
}

void onNewBackground() {
	if (!ffmpegAvailable()) {
		Loader::get()->queueInMainThread([]() {
			FLAlertLayer::create(
				"An error has occured!",
				"Please install FFmpeg and add it to your system environment variables: https://ffmpeg.org/download.html",
				"Okay"
			)->show();
		});
		
		return;
	}
	
	auto videoPath = Mod::get()->getSettingValue<std::filesystem::path>("bgVideoPath");
	auto fps = Mod::get()->getSettingValue<int64_t>("fps");
	
	if (!std::filesystem::exists(videoPath)) {
		Loader::get()->queueInMainThread([]() {
			FLAlertLayer::create(
				"An error has occured!",
				"Failed to load video background file: Path does not exist",
				"Okay"
			)->show();
		});
		
		return;
	}
	
	auto saveDir = Mod::get()->getSaveDir();
	auto framesDir = saveDir / "menuVideoBgFrames";
	
	std::error_code errCode;
	
	if (std::filesystem::exists(framesDir)) {
		cleanFramesDir(framesDir, errCode);
		
		if (errCode) {
			Loader::get()->queueInMainThread([framesDir, errCode]() {
				FLAlertLayer::create(
					"An error has occured!",
					fmt::format("Failed to delete folder \"{}\" with error \"{}\"", framesDir, errCode.message()),
					"Okay"
				)->show();
			});
			
			return;
		}
	}
	
	if (!std::filesystem::exists(saveDir)) {
		std::filesystem::create_directories(saveDir, errCode);
	}
	
	std::filesystem::create_directories(framesDir, errCode);
	
	if (errCode) {
		Loader::get()->queueInMainThread([framesDir, errCode]() {
			FLAlertLayer::create(
				"An error has occured!",
				fmt::format("Failed to create folder \"{}\" with error \"{}\"", framesDir, errCode.message()),
				"Okay"
			)->show();
		});
		
		return;
	}
	
	auto outputImage = framesDir / "output_%04d.png";
	auto logPath = framesDir / "ffmpeg.log";
	
	auto command = fmt::format("start cmd /c \"ffmpeg -i \"{}\" -vf \"fps={}\" \"{}\"\" & exit", videoPath, fps, outputImage);
	auto exitCode = std::system(command.c_str());
	
	Loader::get()->queueInMainThread([]() {
		FLAlertLayer::create(
			"Executing...",
			"Wait for the terminal process to finish then you can close it",
			"Okay"
		)->show();
	});
}

void applyBackground(CCLayer* layer, const char* bgNodeId) {
	geode::log::info("Applying background to node \"{}\"", bgNodeId);
	
	auto saveDir = Mod::get()->getSaveDir();
	auto framesDir = saveDir / "menuVideoBgFrames";
	auto imagePath = framesDir / "output_0001.png";
	
	if (!std::filesystem::exists(imagePath)) {
		FLAlertLayer::create(
			"An error has occured!",
			"Failed to find background image",
			"Okay"
		)->show();
		
		return;
	}
	
	auto fps = Mod::get()->getSettingValue<int64_t>("fps");
	auto cSprite = CCSprite::create(imagePath.string().c_str());
	
	if (auto bg = layer->getChildByID(bgNodeId)) {
		bg->setVisible(false);
	}
	
	auto winSize = CCDirector::sharedDirector()->getWinSize();
	auto sprSize = cSprite->getContentSize();
	
	cSprite->setScaleX(winSize.width / sprSize.width);
	cSprite->setScaleY(winSize.height / sprSize.height);
	cSprite->setPosition({winSize.width / 2.0f, winSize.height / 2.0f});
	
	auto updater = new BackgroundUpdater();
	updater->fps = fps;
	updater->cSprite = cSprite;
	updater->startUpdating();
	
	cSprite->addChild(updater);
	
	layer->addChild(cSprite, -1);
}

class $modify(MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) {
			return false;
		}
		
		applyBackground(this, "main-menu-bg");
		
		return true;
	} 
};
