#include "EmulatorRenderer.h"
#include "EmulatorFileHandler.h"
#include "EmulatorSettings.h"
#include <string>
#include <sstream>
#include "Vector4.h"
#include "TextureLoader.h"

#define CROSS_TEXTURE_FILE_NAME						L"Assets/pad_cross.dds"
#define BUTTONS_TEXTURE_FILE_NAME					L"Assets/pad_buttons.dds"
#define SS_TEXTURE_FILE_NAME						L"Assets/pad_start_select.dds"
#define L_TEXTURE_FILE_NAME							L"Assets/pad_l_button.dds"
#define R_TEXTURE_FILE_NAME							L"Assets/pad_r_button.dds"
#define STICK_TEXTURE_FILE_NAME						L"Assets/ThumbStick.dds"
#define STICK_CENTER_TEXTURE_FILE_NAME				L"Assets/ThumbStickCenter.dds"

#define AUTOSAVE_INTERVAL				10.0f

using namespace Windows::Foundation;

HANDLE swapEvent = NULL;
HANDLE updateEvent = NULL;

bool lastSkipped = false;

int framesNotRendered = 0;
bool monitorTypeSkipped = false;

void ContinueEmulation(void)
{
	if(swapEvent && updateEvent)
	{
		ResetEvent(swapEvent);
		SetEvent(updateEvent);
	}
}

extern u8 *pix;
size_t gbaPitch;

inline void cpyImg32( unsigned char *dst, unsigned int dstPitch, unsigned char *src, unsigned int srcPitch, unsigned short width, unsigned short height )
{
	// fast, iterative C version
	// copies an width*height array of visible pixels from src to dst
	// srcPitch and dstPitch are the number of garbage bytes after a scanline
	register unsigned short lineSize = width<<2;

	while( height-- ) {
		memcpy( dst, src, lineSize );
		src += srcPitch;
		dst += dstPitch;
	}
}

namespace Win8Snes9x
{
	//extern bool timeMeasured;
	extern bool autosaving;
	extern bool gbaROMLoaded;

	EmulatorRenderer ^EmulatorRenderer::renderer = nullptr;

	EmulatorRenderer ^EmulatorRenderer::GetInstance(void)
	{
		return renderer;
	}

	EmulatorRenderer::EmulatorRenderer()
		: emulator(EmulatorGame::GetInstance()),
		frontbuffer(0), controller(nullptr), 
		elapsedTime(0.0f), frames(0), autosaveElapsed(0.0f)
	{ 
		this->gameTime = ref new GameTime();
		renderer = this;

		this->waitEvent = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);

		swapEvent = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);
		updateEvent = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);

		/*this->stopThread = false;
		this->autosaveDoneEvent = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);
		this->autosaveEvent = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);
		this->threadAction = ThreadPool::RunAsync(ref new WorkItemHandler([this](IAsyncAction ^action)
		{
		this->AutosaveAsync();
		}), WorkItemPriority::High, WorkItemOptions::None);*/
	}

	EmulatorRenderer::~EmulatorRenderer(void)
	{
		if(this->m_d3dContext)
		{
			this->m_d3dContext->Unmap(this->buffers[(this->frontbuffer + 1) % 2].Get(), 0);
		}

		CloseHandle(this->waitEvent);
		CloseHandle(swapEvent);
		CloseHandle(updateEvent);

		/*this->stopThread = true;
		SetEvent(this->autosaveEvent);
		WaitForSingleObjectEx(this->autosaveDoneEvent, INFINITE, false);*/
		
		delete this->dxSpriteBatch;
		this->dxSpriteBatch = nullptr;
	}

	void EmulatorRenderer::Initialize(SwapChainBackgroundPanel ^swapChainPanel) 
	{
		Direct3DBase::Initialize(swapChainPanel);

		this->emulator->ResizeBuffer(this->m_renderTargetSize.Width, this->m_renderTargetSize.Height);
		this->controller = this->emulator->GetVirtualController();

		this->width = this->m_renderTargetSize.Width;
		this->height = this->m_renderTargetSize.Height;

		if(!this->dxSpriteBatch)
		{
			this->dxSpriteBatch = new DXSpriteBatch(this->m_d3dDevice.Get(), this->m_d3dContext.Get(), this->width, this->height);
		}else
		{
			this->dxSpriteBatch->OnResize(this->width, this->height);
		}
	}

	GameTime ^EmulatorRenderer::GetGameTime(void)
	{
		return this->gameTime;
	}

	void EmulatorRenderer::HandleDeviceLost(void)
	{

	}

	void EmulatorRenderer::CreateDeviceResources()
	{
		Direct3DBase::CreateDeviceResources();

		this->m_d3dDevice->GetImmediateContext1(this->m_d3dContext.GetAddressOf());		
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			STICK_TEXTURE_FILE_NAME,
			this->stickResource.GetAddressOf(), 
			this->stickSRV.GetAddressOf()
			);
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			STICK_CENTER_TEXTURE_FILE_NAME,
			this->stickCenterResource.GetAddressOf(), 
			this->stickCenterSRV.GetAddressOf()
			);
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			CROSS_TEXTURE_FILE_NAME,
			this->crossResource.GetAddressOf(), 
			this->crossSRV.GetAddressOf()
			);
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			BUTTONS_TEXTURE_FILE_NAME,
			this->buttonsResource.GetAddressOf(), 
			this->buttonsSRV.GetAddressOf()
			);
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			SS_TEXTURE_FILE_NAME,
			this->startSelectResource.GetAddressOf(), 
			this->startSelectSRV.GetAddressOf()
			);
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			L_TEXTURE_FILE_NAME,
			this->lButtonResource.GetAddressOf(), 
			this->lButtonSRV.GetAddressOf()
			);
		
		LoadTextureFromFile(
			this->m_d3dDevice.Get(), 
			R_TEXTURE_FILE_NAME,
			this->rButtonResource.GetAddressOf(), 
			this->rButtonSRV.GetAddressOf()
			);

		// Create Textures and SRVs for front and backbuffer
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));

		desc.ArraySize = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		//desc.Format = DXGI_FORMAT_B5G6R5_UNORM;
		desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
		desc.Width = 241;//EXT_WIDTH;
		desc.Height = 162;//EXT_HEIGHT;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DYNAMIC;

		DX::ThrowIfFailed(
			this->m_d3dDevice->CreateTexture2D(&desc, nullptr, this->buffers[0].GetAddressOf())
			);
		DX::ThrowIfFailed(
			this->m_d3dDevice->CreateTexture2D(&desc, nullptr, this->buffers[1].GetAddressOf())
			);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

		DX::ThrowIfFailed(
			this->m_d3dDevice->CreateShaderResourceView(this->buffers[0].Get(), &srvDesc, this->bufferSRVs[0].GetAddressOf())
			);
		DX::ThrowIfFailed(
			this->m_d3dDevice->CreateShaderResourceView(this->buffers[1].Get(), &srvDesc, this->bufferSRVs[1].GetAddressOf())
			);

		// Map backbuffer so it can be unmapped on first update
		int backbuffer = (this->frontbuffer + 1) % 2;
		this->backbufferPtr = (uint8 *) this->MapBuffer(backbuffer, &this->pitch);
		pix = this->backbufferPtr;

		D3D11_BLEND_DESC blendDesc;
		ZeroMemory(&blendDesc, sizeof(D3D11_BLEND_DESC));

		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;

		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		DX::ThrowIfFailed(
			this->m_d3dDevice->CreateBlendState(&blendDesc, this->alphablend.GetAddressOf())
			);
	}

	void EmulatorRenderer::UpdateForWindowSizeChange()
	{
		Direct3DBase::UpdateForWindowSizeChange();

		//float scale = ((int)Windows::Graphics::Display::DisplayProperties::ResolutionScale) / 100.0f;
		this->width = m_renderTargetSize.Width;// width * scale;
		this->height = m_renderTargetSize.Height;//height * scale;

		if(!this->dxSpriteBatch)
		{
			this->dxSpriteBatch = new DXSpriteBatch(this->m_d3dDevice.Get(), this->m_d3dContext.Get(), this->width, this->height);
		}else
		{
			this->dxSpriteBatch->OnResize(this->width, this->height);
		}

		//this->emulator->ResizeBuffer(this->width, this->height);
	}

	void EmulatorRenderer::FPSCounter(void)
	{
		float lastElapsed = this->gameTime->GetLastFrameElapsed();
		this->fpsElapsedTime += lastElapsed;
		if(this->fpsElapsedTime >= 1.0)
		{
			this->fps = this->frames;
			this->frames = 0;
			this->fpsElapsedTime -= 1.0;
		}
	}

	/*void EmulatorRenderer::MeasureTime(void)
	{
		float lastElapsed = this->gameTime->GetLastFrameElapsed();
		this->elapsedTimeMeasure += lastElapsed;
		if(this->elapsedTimeMeasure >= 3.0)
		{
			timeMeasured = true;
			if(this->fps < 34)
			{
				EnableLowDisplayRefreshMode(true);
			}
		}
	}*/

	void EmulatorRenderer::AutosaveAsync(void)
	{
		/*WaitForSingleObjectEx(this->autosaveEvent, INFINITE, false);
		while(!this->stopThread)
		{
		this->emulator->Pause();
		SaveSRAMAsync().wait();
		int oldSlot = SavestateSlot;
		SavestateSlot = AUTOSAVESTATE_SLOT;
		SaveStateAsync().wait();
		SavestateSlot = oldSlot;
		Settings.Mute = !SoundEnabled();

		SetEvent(this->autosaveDoneEvent);
		WaitForSingleObjectEx(this->autosaveEvent, INFINITE, false);
		}
		SetEvent(this->autosaveDoneEvent);*/
	}

	void EmulatorRenderer::Autosave(void)
	{
		if(AutosavingEnabled() && !this->emulator->IsPaused() && IsROMLoaded())
		{
			float lastElapsed = this->gameTime->GetLastFrameElapsed();
			this->autosaveElapsed += lastElapsed;
			if(this->autosaveElapsed >= AUTOSAVE_INTERVAL)
			{
				this->autosaveElapsed -= AUTOSAVE_INTERVAL;

				create_task([this]()
				{
					SaveSRAMCopyAsync();
				});
			}
		}
	}

	void EmulatorRenderer::Update(void)
	{
		this->gameTime->Update();

		float timeDelta = this->gameTime->GetLastFrameElapsed();


		/*if(!timeMeasured)
		{
			this->MeasureTime();
		}*/

		if(!emulator->IsPaused())
		{			
			this->elapsedTime += timeDelta;

			this->lastElapsed = timeDelta;
			
			systemFrameSkip = GetPowerFrameSkip();
			float targetFPS = 55.0f;
			if(GetMonitorType() == 0)
			{
				systemFrameSkip = systemFrameSkip * 2 + 1;
				targetFPS = 28.0f;
			}
			if(GetFrameSkip() == -1 && GetPowerFrameSkip() == 0)
			{
				if(!lastSkipped && (this->lastElapsed * 1.0f) > (1.0f / targetFPS))
				{
					int skip = (int)((this->lastElapsed * 1.0f) / (1.0f / targetFPS));				
					systemFrameSkip += (skip < 2) ? skip : 2;
					//systemFrameSkip++;
					lastSkipped = true;
				}else
				{
					lastSkipped = false;
				}
			}else if(GetFrameSkip() >= 0)
			 {
				systemFrameSkip += GetFrameSkip();
			 }

			this->Autosave();

			this->emulator->Update();
		}
		this->FPSCounter();
	}
	void EmulatorRenderer::Render()
	{
		m_d3dContext->OMSetRenderTargets(
			1,
			m_renderTargetView.GetAddressOf(),
			m_depthStencilView.Get()
			);

		const float black[] = { 0.0f, 0.0f, 0.0f, 1.000f };
		m_d3dContext->ClearRenderTargetView(
			m_renderTargetView.Get(),
			black
			);

		m_d3dContext->ClearDepthStencilView(
			m_depthStencilView.Get(),
			D3D11_CLEAR_DEPTH,
			1.0f,
			0
			);

		if(!this->emulator->IsPaused()/* || autosaving*/)
		{
			if((GetMonitorType() != MONITOR_120HZ) || (monitorTypeSkipped))
			{
				monitorTypeSkipped = false;
				if(framesNotRendered >= GetPowerFrameSkip())
				{
					framesNotRendered = 0;
					WaitForSingleObjectEx(swapEvent, INFINITE, false);

					int backbuffer = this->frontbuffer;
					this->frontbuffer = (this->frontbuffer + 1) % 2;

					uint8 *buffer = (uint8 *) this->MapBuffer(backbuffer, &gbaPitch);
					this->backbufferPtr = buffer;
					this->pitch = gbaPitch;

					pix = buffer;

					this->m_d3dContext->Unmap(this->buffers[this->frontbuffer].Get(), 0);

					SetEvent(updateEvent);
				}else
				{
					framesNotRendered++;
				}
			}else
			{
				monitorTypeSkipped = true;
			}
		}

		int height = this->height * (GetImageScale() / 100.0f);
		int width;
		switch(GetAspectRatio())
		{
		default:
		case AspectRatioMode::Original:
			if(gbaROMLoaded)
			{
				width = (int)(height * (240.0f / 160.0f));
			}else
			{
				width = (int)(height * (160.0f / 144.0f));
			}
			break;
		case AspectRatioMode::Stretch:
			width = this->width * (GetImageScale() / 100.0f);
			break;
		case AspectRatioMode::FourToThree:
			width = (int)(height * (4.0f / 3.0f));
			break;
		case AspectRatioMode::FiveToFour:
			width = (int)(height * (5.0f / 4.0f));
			break;
		case AspectRatioMode::One:
			width = height;
			break;
		}
		int leftOffset = (this->width - width) / 2;
		RECT rect;
		rect.left = leftOffset;
		rect.right = width + leftOffset;
		rect.top = 0;
		rect.bottom = height;

		RECT source;
		if(gbaROMLoaded)
		{
			source.left = 0;
			source.right = 240;
			source.top = 2;
			source.bottom = 161;
		}else
		{
			source.left = 0;
			source.right = 160;
			source.top = 2;
			source.bottom = 144;
		}

		this->controller->GetButtonsRectangle(&buttonsRectangle);
		this->controller->GetCrossRectangle(&crossRectangle);
		this->controller->GetStartSelectRectangle(&startSelectRectangle);
		this->controller->GetLRectangle(&lRectangle);
		this->controller->GetRRectangle(&rRectangle);

		XMFLOAT4A colorf = XMFLOAT4A(1.0f, 1.0f, 1.0f, GetControllerOpacity() / 100.0f);
		XMFLOAT4A colorf2 = XMFLOAT4A(1.0f, 1.0f, 1.0f, (GetControllerOpacity() / 100.0f) + 0.2f);
		XMVECTOR colorv = XMLoadFloat4A(&colorf);
		XMVECTOR colorv2 = XMLoadFloat4A(&colorf2);
		
		// Render last frame to screen
		Color white(1.0f, 1.0f, 1.0f, 1.0f);
		Color color(1.0f, 1.0f, 1.0f, GetControllerOpacity() / 100.0f);
		Color color2(1.0f, 1.0f, 1.0f, (GetControllerOpacity() / 100.0f) + 0.2f);
		

		// Render last frame to screen
		this->dxSpriteBatch->Begin();

		Engine::Rectangle sourceRect(source.left, source.top, source.right - source.left, source.bottom - source.top);
		Engine::Rectangle targetRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);

		this->dxSpriteBatch->Draw(targetRect, &sourceRect, this->bufferSRVs[this->frontbuffer].Get(), this->buffers[this->frontbuffer].Get(), white);

		if(TouchControlsEnabled())
		{
			Engine::Rectangle buttonsRect (this->buttonsRectangle.left, this->buttonsRectangle.top, this->buttonsRectangle.right - this->buttonsRectangle.left, this->buttonsRectangle.bottom - this->buttonsRectangle.top);

			ComPtr<ID3D11Texture2D> tex;
			this->buttonsResource.As(&tex);
			this->dxSpriteBatch->Draw(buttonsRect, this->buttonsSRV.Get(), tex.Get(), color);

			int dpad = GetDPadStyle();
			if(dpad == 0)
			{
				Engine::Rectangle crossRect (this->crossRectangle.left, this->crossRectangle.top, this->crossRectangle.right - this->crossRectangle.left, this->crossRectangle.bottom - this->crossRectangle.top);

				ComPtr<ID3D11Texture2D> tex;
				this->crossResource.As(&tex);
				this->dxSpriteBatch->Draw(crossRect, this->crossSRV.Get(), tex.Get(), color);
			}
			else if(dpad == 1 || (dpad == 2 && this->controller->StickFingerDown()))
			{
				RECT centerRect;
				RECT stickRect;
				this->controller->GetStickRectangle(&stickRect);
				this->controller->GetStickCenterRectangle(&centerRect);

				Engine::Rectangle stickRectE (stickRect.left, stickRect.top, stickRect.right - stickRect.left, stickRect.bottom - stickRect.top);
				Engine::Rectangle stickRectCenterE (centerRect.left, centerRect.top, centerRect.right - centerRect.left, centerRect.bottom - centerRect.top);

				ComPtr<ID3D11Texture2D> tex;
				this->stickResource.As(&tex);
				ComPtr<ID3D11Texture2D> tex2;
				this->stickCenterResource.As(&tex2);
				this->dxSpriteBatch->Draw(stickRectCenterE, this->stickCenterSRV.Get(), tex2.Get(), color2);
				this->dxSpriteBatch->Draw(stickRectE, this->stickSRV.Get(), tex.Get(), color);
			}

			Engine::Rectangle startSelectRectE (startSelectRectangle.left, startSelectRectangle.top, startSelectRectangle.right - startSelectRectangle.left, startSelectRectangle.bottom - startSelectRectangle.top);

			ComPtr<ID3D11Texture2D> texSS;
			this->startSelectResource.As(&texSS);

			this->dxSpriteBatch->Draw(startSelectRectE, this->startSelectSRV.Get(), texSS.Get(), color);

			if(gbaROMLoaded)
			{
				Engine::Rectangle lRectE (lRectangle.left, lRectangle.top, lRectangle.right - lRectangle.left, lRectangle.bottom - lRectangle.top);
				Engine::Rectangle rRectE (rRectangle.left, rRectangle.top, rRectangle.right - rRectangle.left, rRectangle.bottom - rRectangle.top);

				ComPtr<ID3D11Texture2D> tex;
				this->lButtonResource.As(&tex);
				ComPtr<ID3D11Texture2D> tex2;
				this->rButtonResource.As(&tex2);
				this->dxSpriteBatch->Draw(lRectE, this->lButtonSRV.Get(), tex.Get(), color);
				this->dxSpriteBatch->Draw(rRectE, this->rButtonSRV.Get(), tex2.Get(), color);
			}
		}

		/*if(ShowingFPS())
		{
			wstringstream wss;
			wss << L"FPS: ";
			wss << this->fps;

			XMFLOAT4A fpscolor = XMFLOAT4A(1.0f, 1.0f, 1.0f, 1.0f);
			XMVECTOR fpscolorA = XMLoadFloat4A(&fpscolor);

			XMFLOAT2A pos = XMFLOAT2A(10.0f, 10.0f);
			XMVECTOR posA = XMLoadFloat2A(&pos);

			XMFLOAT2A origin = XMFLOAT2A(0.0f, 0.0f);
			XMVECTOR originA = XMLoadFloat2A(&origin);

			this->font->DrawString(this->spriteBatch, wss.str().c_str(), posA, fpscolorA, 0.0f, originA, 1.0f);
		}*/

		this->dxSpriteBatch->End();

		this->Present();

		frames++;
	}

	void *EmulatorRenderer::MapBuffer(int index, size_t *rowPitch)
	{
		D3D11_MAPPED_SUBRESOURCE map;
		ZeroMemory(&map, sizeof(D3D11_MAPPED_SUBRESOURCE));

		DX::ThrowIfFailed(
			this->m_d3dContext->Map(this->buffers[index].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map)
			);

		*rowPitch = map.RowPitch;
		return map.pData;
	}

}

void systemDrawScreen() 
{ 
	///*EnterCriticalSection(&swapCS);

	//cpyImg32(tmpBuf + 964, 964, pix + 964, 964, 240, 160);

	//LeaveCriticalSection(&swapCS);*/

	LeaveCriticalSection(&pauseSync);

	SetEvent(swapEvent);

	WaitForSingleObjectEx(updateEvent, INFINITE, false);

	EnterCriticalSection(&pauseSync);
}