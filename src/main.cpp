#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>

#include <filesystem>
#include <chrono>

using namespace geode::prelude;

int getFrameCount(const std::filesystem::path& framesDir);

class BackgroundUpdater : public CCLayer {
public:
	CCTextureCache* textureCache = CCTextureCache::sharedTextureCache();
	CCSprite* cSprite = nullptr;
	std::chrono::steady_clock::time_point startTimestamp = std::chrono::steady_clock::now();
	std::filesystem::path framesDir;
	int frameCount;
	int renderFps;
	int fps;
	
	void updateBackground(float) {
		if (!cSprite) {
			geode::log::warn("cSprite is not set!");
			return;
		}
		
		auto currentTimestamp = std::chrono::steady_clock::now();
		auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(currentTimestamp - startTimestamp).count();
		auto frameIndex = (static_cast<int>((elapsedMilliseconds * fps * 0.001f)) % frameCount) + 1;
		//                                   elapsedMilliseconds / (fps / 1000.0f)
		
		auto imagePath = framesDir / fmt::format("{:04d}.png", frameIndex);
		
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
		auto saveDir = Mod::get()->getSaveDir();
		
		framesDir = saveDir / "menuVideoBgFrames";
		frameCount = getFrameCount(framesDir);
		
		geode::log::info("Initializing scheduler | fps: {}, frameCount: {}", fps, frameCount);
		BackgroundUpdater::schedule(schedule_selector(BackgroundUpdater::updateBackground), 1.0f / 60.0f);
		geode::log::info("Done!");
	}
};

int getFrameCount(const std::filesystem::path& framesDir) {
	auto count = 0;
	
	for (const auto& entry : std::filesystem::directory_iterator(framesDir)) {
		if (entry.is_regular_file() && entry.path().extension() == ".png") {
			count++;
		}
	}
	
	return count;
}

bool ffmpegAvailable() {
	return std::system("ffmpeg -version >nul 2>&1") == 0;
}

int cleanFramesDir(const std::filesystem::path& framesDir, geode::Loader* loader) {
	auto textureCache = CCTextureCache::sharedTextureCache();
	
	for (const auto& entry : std::filesystem::directory_iterator(framesDir)) {
		if (entry.is_regular_file() && entry.path().extension() == ".png") {
			textureCache->removeTextureForKey(entry.path().string().c_str());
		}
	}
	
	std::error_code errCode;
	std::filesystem::remove_all(framesDir, errCode);
	
	if (errCode) {
		loader->queueInMainThread([framesDir, errCode]() {
			FLAlertLayer::create(
				"An error has occured!",
				fmt::format("Failed to delete folder \"{}\" with error \"{}\"", framesDir, errCode.message()),
				"Okay"
			)->show();
		});
		
		return 1;
	}
	
	return 0;
}

int makeDirectory(const std::filesystem::path& dir, geode::Loader* loader) {
	if (!std::filesystem::exists(dir)) {
		std::error_code errCode;
		std::filesystem::create_directories(dir, errCode);
		
		if (errCode) {
			loader->queueInMainThread([dir, errCode]() {
				FLAlertLayer::create(
					"An error has occured!",
					fmt::format("Failed to create folder \"{}\" with error \"{}\"", dir.string(), errCode.message()),
					"Okay"
				)->show();
			});
			
			return 1;
		}
	}
	
	return 0;
}

void loadBackground() {
	auto loader = Loader::get();
	
	if (!ffmpegAvailable()) {
		loader->queueInMainThread([]() {
			geode::createQuickPopup(
				"An error has occured!",
				"Please <cg>install FFmpeg</c> and <cr>add it to your system environment variables</c>: <cj>https://ffmpeg.org/download.html</c>",
				"Okay", "Open Link",
				[](auto, bool openLink) {
					if (openLink) {
						web::openLinkInBrowser("https://ffmpeg.org/download.html");
					}
				}
			);
		});
		
		return;
	}
	
	auto videoPath = Mod::get()->getSettingValue<std::filesystem::path>("bgVideoPath");
	auto fps = Mod::get()->getSettingValue<int64_t>("fps");
	
	if (!std::filesystem::exists(videoPath)) {
		loader->queueInMainThread([videoPath]() {
			FLAlertLayer::create(
				"An error has occured!",
				fmt::format("Failed to load video background file, \"{}\" does not exist", videoPath),
				"Okay"
			)->show();
		});
		
		return;
	}
	
	auto saveDir = Mod::get()->getSaveDir();
	auto framesDir = saveDir / "menuVideoBgFrames";
	
	if (std::filesystem::exists(framesDir) && cleanFramesDir(framesDir, loader) != 0) {
		return;
	}
	
	if (makeDirectory(saveDir, loader) != 0) {
		return;
	}
	
	if (makeDirectory(framesDir, loader) != 0) {
		return;
	}
	
	auto outputImage = framesDir / "%04d.png";
	auto logPath = framesDir / "ffmpeg.log";
	
	auto command = fmt::format("start cmd /c \"ffmpeg -i \"{}\" -vf \"fps={}\" \"{}\"\" & exit", videoPath, fps, outputImage);
	
	std::system(command.c_str());
	
	loader->queueInMainThread([]() {
		FLAlertLayer::create(
			"Executing...",
			"Wait for the terminal process to finish then you can close it using CTRL+D",
			"Okay"
		)->show();
	});
}

void applyBackground(CCLayer* layer, const char* bgNodeId) {
	geode::log::info("Applying background to node \"{}\"", bgNodeId);
	
	auto saveDir = Mod::get()->getSaveDir();
	auto framesDir = saveDir / "menuVideoBgFrames";
	auto imagePath = framesDir / "0001.png";
	
	if (!std::filesystem::exists(imagePath)) {
		FLAlertLayer::create(
			"An error has occured!",
			"Failed to find background image",
			"Okay"
		)->show();
		
		return;
	}
	
	auto fps = Mod::get()->getSettingValue<int64_t>("fps");
	auto renderFps = Mod::get()->getSettingValue<int64_t>("renderFps");
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
	updater->renderFps = renderFps;
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
