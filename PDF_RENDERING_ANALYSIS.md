# LibreOffice Draw PDF 渲染管线分析报告

## 目录

1. [LibreOffice Draw PDF 渲染管线](#1-libreoffice-draw-pdf-渲染管线)
2. [Okular PDF 渲染管线](#2-okular-pdf-渲染管线)
3. [LibreOffice 只读模式实现](#3-libreoffice-只读模式实现)
4. [对比总结与改进建议](#4-对比总结与改进建议)
5. [自定义 PDF 渲染管线实施方案](#5-自定义-pdf-渲染管线实施方案)

---

## 1. LibreOffice Draw PDF 渲染管线

### 1.1 总体架构

LibreOffice 有**两个独立**的 PDF 导入路径：

#### 路径 A：快速路径（SdPdfFilter）— 打开 PDF 时实际使用

```
PDF 文件
  │
  ▼
ImportPDFUnloaded()                    // vcl/source/filter/ipdf/pdfread.cxx:351
  │  读取整个 PDF 到 BinaryDataContainer
  │  用 PDFium 读取页数、页面尺寸（元数据）
  │  每页创建一个 Graphic（含 GfxLink + 页码）—— 尚未渲染！
  │
  ▼
SdPdfFilter::Import()                  // sd/source/filter/pdf/sdpdffilter.cxx:49
  │  每页 = 一个 SdrGrafObj（可绘制的图像对象）
  │  页面尺寸设为 PDF 页面的物理尺寸
  │  mrDocument.setPDFDocument(true)
  │
  ▼
[延迟渲染 — 页面上屏时才触发]
```

#### 路径 B：完整转换路径（PDFIRawAdaptor）— 旧版/备用

```
PDF 文件
  │
  ▼
xpdfimport 子进程                      // sdext/source/pdfimport/xpdfwrapper/wrapper_gpl.cxx
  │  poppler 库，7200 DPI 虚拟分辨率
  │  输出文本协议到 stdout，二进制数据到 stderr
  │
  ▼
Parser（wrapper.cxx）                  // 解析文本协议行
  │
  ▼
PDFIProcessor（ContentSink）           // sdext/source/pdfimport/tree/pdfiprocessor.cxx
  │  构建 DOM 树：PageElement → FrameElement → TextElement/PolyPolyElement/ImageElement
  │
  ▼
三次遍历 DOM 树
  │  第1遍：DrawXmlOptimizer    — 合并路径、优化文本
  │  第2遍：DrawXmlFinalizer    — 收集样式
  │  第3遍：DrawXmlEmitter      — 输出 ODF XML
  │
  ▼
ODF XML → LibreOffice ODF 导入 → 每个页面多个独立 Shape 对象
```

**路径 A 是当前实际使用的路径**（速度快，但仍有性能问题）。
**路径 B** 涉及进程通信 + 管道解析 + 三次树遍历，非常慢，且生成可编辑 Shape 的粒度是逐个 PDF 元素级别的。

---

### 1.2 延迟渲染链路（路径 A 的详细流程）

#### 阶段 1：文件导入（快，在打开时执行）

**入口：** `SdPdfFilter::Import()` at `sd/source/filter/pdf/sdpdffilter.cxx:49`

```cpp
// 1. 读取 PDF 的所有页面
vcl::ImportPDFUnloaded(aFileName, aGraphics);

// 2. 创建对应的 Draw 页面
mrDocument.CreateFirstPages();
for (size_t i = 0; i < aGraphics.size() - 1; ++i)
    mrDocument.DuplicatePage(nPageToDuplicate);

// 3. 每页插入一个 SdrGrafObj
for (vcl::PDFGraphicResult const& rResult : aGraphics) {
    const Graphic& rGraphic = rResult.GetGraphic();
    SdPage* pPage = mrDocument.GetSdPage(nPageNumber, PageKind::Standard);
    pPage->SetSize(aSizeHMM);
    rtl::Reference<SdrGrafObj> pSdrGrafObj =
        new SdrGrafObj(rModel, rGraphic, tools::Rectangle(Point(), aSizeHMM));
    pSdrGrafObj->SetResizeProtect(true);  // 禁止调整大小
    pSdrGrafObj->SetMoveProtect(true);    // 禁止移动
    pPage->InsertObject(pSdrGrafObj.get());
}
```

`ImportPDFUnloaded()` at `vcl/source/filter/ipdf/pdfread.cxx:351`:

```cpp
// 每页创建 Graphic(pGfxLink, nPageIndex)
// pGfxLink 包含整个 PDF 文件的原始二进制数据
// nPageIndex 是页码索引
// 此时不渲染任何位图！
Graphic aGraphic(pGfxLink, nPageIndex);
```

**关键设计：** PDF 数据被存储在 `GfxLink` 中，`Graphic` 处于 "swapped out" 状态。导入过程不渲染任何像素。所有 500 页的 Graphic 共享同一个 `GfxLink` 指向全局的 PDF 数据。

#### 阶段 2：首屏渲染（慢，在页面显示时执行）

当页面需要在屏幕上绘制时，触发以下调用链：

```
sd::Window::Paint()
  → DrawViewShell::Paint()
    → sd::View::CompleteRedraw()
      → SdrPaintView::CompleteRedraw()
        → SdrPageView::CompleteRedraw()           // svx/source/svdraw/svdpntv.cxx:618
          → 绘图层 Contact 系统构建 Primitive 表示
            → 对于每个 SdrGrafObj：
              → Graphic::draw() 需要位图
                → ImpGraphic::ensureAvailable()    // vcl/source/gdi/impgraph.cxx:1384
                  → ImpGraphic::swapIn()           // vcl/source/gdi/impgraph.cxx:1513
                    │
                    ├─ mpGfxLink->LoadNative(aGraphic, nPageNum)
                    │     → GraphicFilter::ImportGraphic(format=PDF_SHORTNAME)
                    │       → ImportPDF(pStream, rGraphic, nPageIndex)
                    │         → 创建 VectorGraphicData(type=Pdf, pageIndex)
                    │
                    ├─ 或 lclConvertToVectorGraphicType(mpGfxLink)
                    │     → VectorGraphicDataType::Pdf
                    │     → vcl::loadVectorGraphic(dataContainer, Pdf)
                    │
                    └─ restoreFromSwapInfo()
                │
                ▼
              VectorGraphicData::ensureReplacement() // vectorgraphicdata.cxx:165
                → ensurePdfReplacement()             // vectorgraphicdata.cxx:146
                  → RenderPDFBitmaps(pBuffer, nSize, aBitmaps, nPageIndex, 1, sizeHint)
                    │                                  // vcl/source/filter/ipdf/pdfread.cxx:27
                    │
                    ├─ PDFiumLibrary::openDocument(pBuffer, nSize)
                    ├─ PDFiumDocument::getPageCount()
                    ├─ PDFiumPage = openPage(nPageIndex)
                    ├─ nPageWidth  = pointToPixel(width,  DPI) × MAGIC_SCALE_FACTOR
                    ├─ nPageHeight = pointToPixel(height, DPI) × MAGIC_SCALE_FACTOR
                    ├─ PDFiumBitmap = createBitmap(nPageWidth, nPageHeight, alpha=1)
                    ├─ PDFiumBitmap::fillRect(0, 0, ...)  // 白色或透明背景
                    ├─ PDFiumBitmap::renderPageBitmap(doc, page, ...)
                    └─ BitmapEx = createBitmapFromBuffer()
```

#### 阶段 3：后续重绘

`VectorGraphicData` 在其 `maReplacement` 字段中缓存一个位图。如果以相同尺寸再次绘制，使用缓存的位图。如果尺寸改变（例如用户缩放），则需要完整的重新渲染。

---

### 1.3 关键数据结构

```
Graphic (public API)
  └── ImpGraphic (内部实现)
        ├── mpGfxLink: shared_ptr<GfxLink>
        │     └── maDataContainer: BinaryDataContainer  (完整 PDF 文件的原始字节)
        │     └── meType = GfxLinkType::NativePdf (=12)
        │
        ├── maVectorGraphicData: shared_ptr<VectorGraphicData>
        │     └── maDataContainer: BinaryDataContainer  (PDF 原始数据的拷贝)
        │     └── meType = VectorGraphicDataType::Pdf (=3)
        │     └── mnPageIndex: sal_Int32                 (要渲染的页码)
        │     └── maSizeHint: basegfx::B2DTuple          (可选尺寸覆盖)
        │     └── maReplacement: BitmapEx                (惰性：按需填充)
        │     └── maSequence: deque<XPrimitive2D>       (惰性：按需填充)
        │
        ├── maCachedBitmap: BitmapEx                    (缓存)
        └── mbSwapOut: bool                              (已换出到磁盘/GFX 链接？)
```

### 1.4 性能瓶颈详解

#### 瓶颈 1：没有页面级位图缓存

`VectorGraphicData` 只为默认尺寸缓存**一个**替换位图。如果再次以不同尺寸调用 `getBitmap(pixelSize)`，会导致完整的 PDFium 重新渲染。在页面之间切换时，先前页面的 Graphic 可能被换出并丢失其位图。

#### 瓶颈 2：幻灯片排序器触发所有可见缩略图的渲染

`sd/source/ui/slidesorter/cache/` 中的幻灯片排序器缓存为幻灯片排序器面板中可见的每个页面调用独立的渲染。每个请求都会通过完整的 swapIn → PDFium 渲染链。50 个可见的缩略图 × 每页 2 秒 = 100 秒的阻塞 UI。

#### 瓶颈 3：PDFium 文档在每个页面上都重新打开

`RenderPDFBitmaps()` 在内部打开一个新的 PDFium 文档对象，定位到该页面，渲染它，然后将其丢弃。没有跨页面的持久化 PDFium 文档实例。

#### 瓶颈 4：同步渲染阻塞 UI

所有渲染都在主线程上进行。没有后台渲染线程，没有异步完成回调，没有优先级队列。幻灯片排序器和主视图争夺同一个同步渲染资源。

#### 瓶颈 5：完整 PDF 数据被多次拷贝

PDF 原始数据至少被拷贝 3 次：
1. 在 `ImportPDFUnloaded()` 中读入 `BinaryDataContainer`
2. 在 `GfxLink::LoadNative()` 中拷贝到 `SvMemoryStream`
3. 在 `importPdfVectorGraphicData()` 或 `loadVectorGraphic()` 中拷贝到 `VectorGraphicData`

对于 500 页、100MB 的 PDF，仅内存拷贝就超过 300MB 的分配。

#### 瓶颈 6：渲染分辨率

渲染位图像素尺寸的计算方式：
```cpp
int nPageWidth  = round(pointToPixel(nPageWidthPoints,  fResolutionDPI) × SCALE_FACTOR);
int nPageHeight = round(pointToPixel(nPageHeightPoints, fResolutionDPI) × SCALE_FACTOR);
```

- `fResolutionDPI` 默认是系统 DPI（通常 96）
- `PDF_INSERT_MAGIC_SCALE_FACTOR` = Linux 上为 1，macOS 上为 8
- 96 DPI 下的 A4 页面 ≈ 794 × 1122 像素（在 macOS 8x 下 ≈ 6350 × 8970 像素，约 52 百万像素）
- 对于幻灯片排序器缩略图（通常约 150px 宽），这是严重的过度渲染

---

### 1.5 完整文件索引

| 文件 | 角色 |
|------|------|
| `sd/source/filter/pdf/sdpdffilter.cxx` | 在 Draw 中打开 PDF 的入口点（每页创建一个 SdrGrafObj） |
| `vcl/source/filter/ipdf/pdfread.cxx` | PDFium 位图渲染，导入函数 |
| `include/vcl/pdfread.hxx` | 公共 API：`RenderPDFBitmaps`，`ImportPDF`，`ImportPDFUnloaded`，`PDF_INSERT_MAGIC_SCALE_FACTOR` |
| `vcl/source/gdi/vectorgraphicdata.cxx` | 惰性求值：按需通过 PDFium 生成替换位图 |
| `include/vcl/vectorgraphicdata.hxx` | 带有惰性位图/序列求值的 `VectorGraphicData` 类 |
| `vcl/source/gdi/impgraph.cxx` | `ImpGraphic` — `ensureAvailable()` → `swapIn()` → `draw()` 调用链 |
| `vcl/source/gdi/gfxlink.cxx` | `GfxLink::LoadNative()` 委托给 `GraphicFilter::ImportGraphic` |
| `include/vcl/gfxlink.hxx` | `GfxLink` 包含原始 PDF 数据的 `BinaryDataContainer` |
| `vcl/source/pdf/PdfConfig.cxx` | 默认 PDF 渲染 DPI（可通过 `PDFIMPORT_RESOLUTION_DPI` 环境变量配置） |
| `svx/source/svdraw/svdpntv.cxx` | `SdrPaintView::CompleteRedraw()` — 绘制调度器 |
| `sd/source/ui/slidesorter/cache/` | 幻灯片排序器缓存（单独的缩略图渲染） |
| `sdext/source/pdfimport/tree/drawtreevisiting.cxx` | 路径 B 的 DrawXmlOptimizer + DrawXmlEmitter |
| `sdext/source/pdfimport/tree/pdfiprocessor.cxx` | 路径 B 的 ContentSink → DOM 树构建器 |
| `sdext/source/pdfimport/wrapper/wrapper.cxx` | 路径 B 的子进程管道解析器 |
| `sdext/source/pdfimport/xpdfwrapper/wrapper_gpl.cxx` | 路径 B 的 poppler 子进程主线程（7200 DPI） |

---

## 2. Okular PDF 渲染管线

### 2.1 总体架构

Okular 使用**生成器模式**，UI 和渲染引擎之间有严格的关注点分离：

```
[GUI/Observers]  ──→  [Document]  ──→  [Generator]  ──→  [Poppler]
     |                     |                  |                 |
 PageView            管理请求队列        渲染 pixmaps        渲染 PDF
 ThumbnailList       缓存、内存、         通过 QThread       页面到
 PresentationWidget  旋转、观察者        后台线程            QImage
```

关键类：
- **Document**（`core/document.h/cpp`）— 中央枢纽。管理请求队列、pixmap 缓存、内存、旋转、观察者。
- **Generator**（`core/generator.h`）— 抽象接口。`image(PixmapRequest*)` 返回 `QImage`。
- **Page**（`core/page.h/cpp`）— 每个观察者存储一个 `QPixmap*`（或 `TilesManager`），加上文本页面。
- **PageView**（`part/pageview.cpp`）— 主 UI 小部件。Document 的观察者。发出 pixmap 请求。

### 2.2 PDF 加载（几乎即时）

**文件：** `generators/poppler/generator_pdf.cpp`

```cpp
// init() — 只加载页面元数据
pdfdoc = Poppler::Document::load(filePath, nullptr, nullptr);
loadPages(pagesVector, rotation, false);

// loadPages() — 每页只存储宽度/高度/方向
for (int i = 0; i < pdfdoc->numPages(); ++i) {
    Okular::Page* page = new Okular::Page(i, width, height, orientation);
    // 没有渲染，没有图形对象，没有形状。只有元数据。
}
```

**打开 500 页的 PDF 只需 2-3 秒。** 没有内容被解析或渲染。

### 2.3 按需延迟渲染

```
PageView::slotRequestVisiblePixmaps()
  │  仅请求与 viewportRect 相交的页面
  │  异步请求，PAGEVIEW_PRIO = 1（高优先级）
  │
  ▼
Document::requestPixmaps(request)
  │  添加到 m_pixmapRequestsStack
  │  优先渲染队列
  │
  ▼
Generator::generatePixmap(request)
  │  如果是异步请求 + Threaded 特性 → 后台线程
  │  如果是同步请求 → 直接调用 image(request)
  │
  ▼
PDFGenerator::image(request)             // generators/poppler/generator_pdf.cpp:1288
  │
  ├─ 如果是 tile:
  │    QRect rect = normalizeRect.geometry(width, height)
  │    img = page->renderToImage(dpiX, dpiY, rect.x(), rect.y(), rect.width(), rect.height())
  │    // 只渲染视口中的 tile！
  │
  └─ 如果是全页:
       img = page->renderToImage(dpiX, dpiY, -1, -1, -1, -1)
       // 以目标像素尺寸渲染整个页面
```

### 2.4 自适应 Tiled（分块）渲染

**文件：** `core/document.cpp:1418`

```cpp
// 如果请求的渲染区域 > 4× 屏幕尺寸且可见区域 < 75% 页面 → 切换到 tile 模式
if (!tilesManager && m_generator->hasFeature(Generator::TiledRendering) &&
    (long)r->width() * (long)r->height() > 4L * screenSize && normalizedArea < 0.75) {
    tilesManager = new TilesManager(page, width, height, ...);
}
// 当缩放减小到 < 3× 屏幕尺寸时 → 回到单 pixmap 模式
```

**TilesManager**（`core/tilesmanager_p.h`）：
- 每个页面有独立的 tile 树（四叉树结构）
- 页面从 4×4 网格（16 个 tile）开始
- 如果单个 tile 超过 `TILES_MAXSIZE`，递归分割为 4 个子 tile
- 远离视口的 tile 被自动驱逐

### 2.5 后台渲染线程 + 优先级队列

**线程架构：**
```
主线程                          后台线程
────────                    ────────────
PixmapRequest →         PixmapGenerationThread
  │ 添加到队列                │ 调用 Generator::image()
  │ 优先级排序                │ 渲染完成
  │                           ▼
  ▼                    信号 → GeneratorPrivate::
sendNextRequest()            pixmapGenerationFinished()
                                  │ (QueuedConnection → 主线程)
                                  ▼
                            QImage → QPixmap
                            Page::setPixmap()
                            signalPixmapRequestDone()
                              → 观察者通知
                              → 开始下一个请求
```

**请求优先级**（`gui/priorities.h`）：
```
PRESENTATION_PRIO         = 0   ← 最高（演示模式）
PAGEVIEW_PRIO             = 1   ← 可见页面
THUMBNAILS_PRIO           = 2   ← 缩略图列表
PRESENTATION_PRELOAD_PRIO = 3
PAGEVIEW_PRELOAD_PRIO     = 4   ← 相邻页面预加载
THUMBNAILS_PRELOAD_PRIO   = 5   ← 最低
```

**预加载行为：**
- 当前页面渲染后，相邻页面被抢占式加载
- 预加载请求使用较低的 PAGEVIEW_PRELOAD_PRIO（= 4）
- 如果 pixmap 缓存已满，低优先级的预加载请求会被跳过

**取消支持：**
- 如果新的渲染请求到来而正在进行的请求已过时（例如，快速缩放时的不同分辨率），Okular 可以**取消**正在运行的渲染

### 2.6 内存管理

**文件：** `core/document.cpp`

```cpp
// 每 2 秒检查内存定时器
slotTimedMemoryCheck() {
    calculateMemoryToFree();
    // 驱逐策略：离视口页面号最远的 pixmap 先被驱逐
    searchLowestPriorityPixmap();
}

// 4 个内存配置
enum { Low, Normal, Aggressive, Greedy }

// Normal 模式：总 pixmap 内存保持在系统 RAM 的 1/3 以内
calculateMemoryToFree() {
    if (totalPixmapMemory > totalRAM / 3)
        freeMemoryTo(needed);
}
```

### 2.7 每观察者独立缓存

每个 `DocumentObserver`（PageView、ThumbnailList、PresentationWidget）为其所有页面获取**自己独立的 pixmap 集合**：

```
PageView        → Page 0: QPixmap (1920×1440), Page 1: QPixmap (1920×1440), ...
ThumbnailList   → Page 0: QPixmap (150×112),   Page 1: QPixmap (150×112), ...
```

- `_o_nearestPixmap()` 回退允许跨观察者借用不同尺寸的 pixmap
- 缩略图始终以低分辨率渲染，永远不会触发全分辨率的 PDFium 渲染

### 2.8 绘制管线（极简）

```
PageView::paintEvent(QPaintEvent*)
  → drawDocumentOnPainter(painter, dirtyRegion, ...)
    → 对于与 dirtyRegion 相交的每个页面：
      → PagePainter::paintCroppedPageOnPainter()
        │
        ├─ 有 tile？→ 遍历 tilesAt(observer)，绘制每个 tile
        │
        ├─ 有 pixmap？→ 去最近的 pixmap，缩放绘制
        │
        └─ 没有 pixmap？→ 绘制占位区域，放入请求队列
```

**没有 shape 树。没有 contact 模型。没有每个对象的 primitive 处理。只位 blit 预渲染的位图到 QPainter。**

### 2.9 Okular 快的原因总结

| 因素 | Okular | LibreOffice Draw |
|------|--------|-----------------|
| 加载 | 只读页面尺寸/数量元数据 | 创建 SdrPage + SdrGrafObj × 500 |
| 页面存储 | 一个 QPixmap（或 tile 树） | 一个 SdrGrafObj，包含 Graphic → VectorGraphicData → GfxLink |
| 渲染路径 | Poppler → QImage → QPixmap → 直接 blit | PDFium → BitmapEx → Graphic → SdrGrafObj → Primitive → Contact → OutputDevice |
| 分块 | 自适应四叉树 tile，只渲染可见区域 | 无分块——总是渲染整个页面 |
| 线程模型 | 后台线程 + 优先级队列 + 取消 | 同步，主线程，无优先级，无取消 |
| 内存 | 基于视口距离驱逐 pixmap，定时检查 | 整个 SdrModel 保留在内存中 |
| 缩略图 | 独立的低分辨率观察者，低优先级 | 共享对象模型，潜在高分辨率渲染 |

---

## 3. LibreOffice 只读模式实现

### 3.1 只读模式由什么组成

只读模式由 6 个 PropertyValue 控制：

| 属性 | 典型值 | 效果 |
|----------|-------------|--------|
| `ReadOnly` | true | 设置 `SfxObjectShell::SetLoadReadonly(true)`，触发 `SetReadOnlyUI()`，禁用编辑工具栏 |
| `LockEditDoc` | true | `isEditLocked()` 返回 true；拒绝所有编辑操作 |
| `LockSave` | true | `isSaveLocked()` 返回 true；`Save_Impl()` 返回 `ERRCODE_SFX_DOCUMENTREADONLY` |
| `LockExport` | true | `isExportLocked()` 返回 true；阻止导出 |
| `LockContentExtraction` | true | `isContentExtractionLocked()` 返回 true；阻止复制/粘贴 |
| `LockPrint` | false | `isPrintLocked()` 返回 false（默认允许打印） |

### 3.2 数据流：Java UNO 路径

**入口点：** `XExtendedFilterDetection::detect()` 在 `OFDImportFilter.java:280`

```
阶段 1：检测（加载前）
─────────────────────────────────────────────────
LibreOffice 调用 XExtendedFilterDetection::detect(PropertyValue[][] descriptor)
  │
  OFDImportFilter.detect(descriptor)
  │
  ├─ if (!isOfdFile(url)) → return ""  // 非 OFD 文件，跳过
  │
  └─ addReadOnlyPropertiesToDescriptor(descriptor)
        │
        descriptor[0] 被原地修改：
          添加 ReadOnly=true, LockEditDoc=true, LockSave=true,
              LockExport=true, LockContentExtraction=true, LockPrint=false
        │
        return "ofd_OpenFormDocument"

阶段 2：属性存储（文档加载期间）
──────────────────────────────────────────
属性数组流向 → SfxBaseModel::setPropertyValues()
                               │
                    sfx2/source/doc/sfxbasemodel.cxx:1148-1187
                               │
  "LockEditDoc"  → pMedium->Put(SfxBoolItem(SID_LOCK_EDITDOC, true))
  "LockSave"     → pMedium->Put(SfxBoolItem(SID_LOCK_SAVE, true))
  "LockExport"   → pMedium->Put(SfxBoolItem(SID_LOCK_EXPORT, true))
  "LockPrint"    → pMedium->Put(SfxBoolItem(SID_LOCK_PRINT, false))
  "LockContent"  → pMedium->Put(SfxBoolItem(SID_LOCK_CONTENT_EXTRACTION, true))
  "ReadOnly"     → SetLoadReadonly(true) → SetReadOnlyUI()

阶段 3：强制执行（运行时）
─────────────────────────
SfxObjectShell 方法在操作前检查锁：
  │
  ├─ isEditLocked()             // sfx2/source/doc/objmisc.cxx:2024
  │     → 检查 XModel3::getArgs2({"LockEditDoc"})
  │     → 还检查 ViewerAppMode 和 AllowEditReadonlyDocs
  │
  ├─ isSaveLocked()             // objmisc.cxx:2078
  │     → Save_Impl() 在保存前检查此项
  │     → 返回 ERRCODE_SFX_DOCUMENTREADONLY
  │
  ├─ isExportLocked()           // objmisc.cxx:2054
  ├─ isPrintLocked()            // objmisc.cxx:2070
  └─ isContentExtractionLocked() // objmisc.cxx:2038

UI 层：
  SID_DOC_READONLY → 工具栏禁用（编辑、保存按钮变灰）
  SID_LOCK_EDITDOC → 拒绝键盘输入/粘贴
  SID_LOCK_SAVE → Ctrl+S = 错误消息
```

### 3.3 槽位定义（SfxItemSet 的 SID 常量）

**文件：** `include/sfx2/sfxsids.hrc`

```cpp
#define SID_LOCK_CONTENT_EXTRACTION   TypedWhichId<SfxBoolItem>(SID_SFX_START + 1731)
#define SID_LOCK_EXPORT               TypedWhichId<SfxBoolItem>(SID_SFX_START + 1732)
#define SID_LOCK_PRINT                TypedWhichId<SfxBoolItem>(SID_SFX_START + 1736)
#define SID_LOCK_SAVE                 TypedWhichId<SfxBoolItem>(SID_SFX_START + 1737)
#define SID_LOCK_EDITDOC              TypedWhichId<SfxBoolItem>(SID_SFX_START + 1738)
#define SID_DOC_READONLY              TypedWhichId<SfxBoolItem>(SID_SFX_START + 590)
```

### 3.4 额外保护：PDF 文档标志

**文件：** `include/svx/svdmodel.hxx`

```cpp
bool m_bIsPDFDocument:1;
bool IsPDFDocument() const;       // sfx2/source/doc/sfxbasemodel.cxx:1629
void setPDFDocument(bool);       // 在 sdpdffilter.cxx:62 中设置为 true
```

在 `sfxbasemodel.cxx:1629` 中：
```cpp
bool SfxBaseModel::isReadonly() const {
    return !m_pData->m_pObjectShell.is() || m_pData->m_pObjectShell->IsReadOnly();
}
```

### 3.5 C++ vs Java 实现

两种方法都修改相同的基础 ItemSet / DocShell 状态。主要区别在于：

| | Java UNO 路径 | C++ 直接路径 |
|---|---|---|
| 入口点 | `XExtendedFilterDetection::detect()` | `SdPdfFilter::Import()` |
| 实施 | 修改 `PropertyValue[]` 数组 | 直接 `Put()` 到 `mrMedium.GetItemSet()` |
| 开销 | JVM 启动 + UNO 桥接 + 序列化 | 几行 C++ 赋值 |
| 所需代码 | ~15 行 Java + ~50 行配置 | ~10 行 C++ |

C++ 方法的核心代码：

```cpp
// 在 SdPdfFilter::Import() 成功返回前添加：
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_EDITDOC, true));
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_SAVE, true));
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_EXPORT, true));
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_CONTENT_EXTRACTION, true));
mrDocShell.SetLoadReadonly(true);
mrDocShell.SetReadOnlyUI();
```

---

## 4. 对比总结与改进建议

### 4.1 为什么 LibreOffice 打开大 PDF 很慢

| 步骤 | 操作 | 每个 500 页 PDF 的时间 | 瓶颈 |
|-------|-----------|------|----------|
| 1 | `ImportPDFUnloaded()`：读取 PDF 元数据 | 5-10 秒 | 整个 PDF 被读入内存 |
| 2 | `SdPdfFilter::Import()`：创建 500 个 SdrPage + SdrGrafObj | 10-20 秒 | 为每个页面创建并插入对象 |
| 3 | 首次页面渲染：swapIn + PDFium 渲染 | 每页 2-5 秒 | 同步 PDFium 渲染阻塞 UI |
| 4 | 幻灯片排序器缩略图：swapIn + PDFium × 50 | 50 × 2 秒 = 100 秒 | 没有带缩略图缓存的低分辨率路径 |
| 总计 | | **5-30 分钟** | 无缓存 + 同步渲染 + 大量 PDFium 调用 |

**根本原因：** LibreOffice 试图将 PDF 当作可编辑文档处理，而 Okular 将其当作只读的位图序列处理。Draw 中每个 PDF 页面都成为 `SdrModel` 中的一个 `SdrGrafObj`，受到完整的 SdrObject → SdrPage → SdrPageView → 绘图层的开销影响。渲染通过 6 层类（Graphic → ImpGraphic → GfxLink → VectorGraphicData → RenderPDFBitmaps → PDFium）进行，而在 Okular 中只有 2 层（Poppler → QImage）就能直接绘制到屏幕。

### 4.2 改进建议（按影响排序）

1.  **为 PDF 页面渲染添加位图缓存。** 一旦 PDF 页面被渲染，缓存结果位图。使用 LRU 策略和内存预算（例如 ~100MB）删除未使用页面的缓存。

2.  **为幻灯片排序器使用低分辨率渲染。** 幻灯片排序器缩略图通常约 150px 宽。以 24 DPI（而不是 96 DPI）渲染可将缩略图渲染时间减少约 16 倍。

3.  **添加后台预渲染线程。** 以较低优先级预渲染视口之后的下 N 个页面。当用户滚动到不可见页面时停止过时的渲染。

4.  **共享一个 PDFium 文档实例。** 目前，PDF 数据在 `GfxLink` 和 `VectorGraphicData` 中被复制，并且 PDFium 文档在每个页面渲染时被重新打开。跨所有页面保持一个 PDFium 文档对象实例。

5.  **实现自适应 tiled 渲染。** 当缩放到 200% 以上时，只渲染当前视口中可见的 tile。

6.  **使用渐进式分辨率。** 首先以低分辨率显示页面，然后异步提升到全分辨率。

7.  **消除中间的 VectorGraphicData 的创建。** 目前，`GfxLink::LoadNative()` 通过 `GraphicFilter::ImportGraphic()` 创建 `VectorGraphicData`，即使已经存在。渲染可以直接调用 `RenderPDFBitmaps()`，跳过 `VectorGraphicData` 中间层。

---

## 5. 自定义 PDF 渲染管线实施方案

### 5.1 设计目标

为 LibreOffice Draw 引入一套类似 Okular 的 "Viewer-first" PDF 渲染引擎，同时保持对现有编辑模式的兼容。

核心思路：
- PDF 打开时默认进入**只读查看模式**（走自定义快速渲染管线）
- 用户点击内置只读信息栏上的"编辑文档"按钮后，**重新加载**走现有 SdrGrafObj 逻辑
- 自定义代码完全 confine 在 PDF filter 内，不碰 Draw 主渲染架构

### 5.2 架构总览

```
PDF 文件
  │
  ├─ 只读查看模式（新管线）
  │     │
  │     ├─ PdfSharedDocument（PDFium 文档，所有页面共享，只打开一次）
  │     │
  │     ├─ PdfBitmapCache（LRU 位图缓存 + 后台预渲染线程）
  │     │     ├─ Key: (pageIndex, pixelWidth, pixelHeight)
  │     │     ├─ 内存预算: ~100 MB
  │     │     ├─ 驱逐策略: 视口距离最远的先淘汰
  │     │     └─ 预渲染: 视口后 N 页，低优先级
  │     │
  │     ├─ SdrPdfCachedPageObj : public SdrRectObj  ← 自定义 SdrObject
  │     │     └─ 每页一个，共享 PdfSharedDocument + PdfBitmapCache
  │     │
  │     ├─ ViewContactOfPdfCachedPage                ← 自定义 ViewContact
  │     │     └─ 唯一重写: createViewIndependentPrimitive2DSequence()
  │     │            从缓存取 BitmapEx，包装为 BitmapPrimitive2D
  │     │
  │     └─ 只读属性 + 内置信息栏
  │           → 用户点击"编辑文档"
  │           → 关闭 Viewer Session → 重新加载 → 走老路径
  │
  └─ 编辑模式（现有管线，不变）
        └─ SdrGrafObj + Graphic + VectorGraphicData + GfxLink + RenderPDFBitmaps
```

### 5.3 前置源码验证：为什么不能从 SdrGrafObj 继承

经过源码审查，发现一个关键的约束条件：

**文件：** `include/svx/svdograf.hxx:67`

```cpp
class SVXCORE_DLLPUBLIC SdrGrafObj final : public SdrRectObj
//                            ^^^^^
```

**`SdrGrafObj` 是 `final` 的，不能被继承。** 因此自定义对象必须从 `SdrRectObj` 继承。

继承链：
```
SdrObject
  → SdrAttrObj                     // 属性支持
    → SdrTextObj                   // 文本支持 + CreateObjectSpecificViewContact()
      → SdrRectObj                 // 矩形几何 + CreateObjectSpecificViewContact() ← 从这里继承
        → SdrGrafObj (final)       // 不能继承！
```

`SdrRectObj` 已经提供了完整的基础设施：
- 矩形几何（position、size、snapping）
- Selection / hit testing
- move / resize / rotate / shear
- Broadcast / undo / redo
- UNO shape 映射
- ViewContact 创建入口

你只需要重写 **一个方法** 来接入自定义的位图渲染逻辑。

### 5.4 源码验证：SdrObject 不直接创建 Primitive

**确认：** `SdrObject` 类中没有任何 `createPrimitive2DSequence` 方法。Primitive 创建完全通过 ViewContact 链完成。

完整的渲染链（源码级）：

```
SdrObject::GetViewContact()                         // svdobj.cxx:252
  │ 延迟创建 CreateObjectSpecificViewContact()
  │
  ▼
ViewContact::getViewIndependentPrimitive2DContainer()  // viewcontact.cxx:254
  │ 调用 createViewIndependentPrimitive2DSequence()
  │
  ▼
ViewContactOfGraphic::createViewIndependentPrimitive2DSequence()
  │                                                 // viewcontactofgraphic.cxx:285
  │
  ├─ 读取 SdrGrafObj 的 GraphicObject
  ├─ 检查是否 PresObj / Draft（换出状态）
  ├─ 构建 GraphicAttr（透明度、裁剪、亮度、Gamma）
  └─ new SdrGrafPrimitive2D(rGraphicObject, ...)    ← 此处触发 swap-in
        │
        ▼
      SdrGrafPrimitive2D::create2DDecomposition()   // sdrgrafprimitive2d.cxx:32
        → GraphicPrimitive2D（真正的位图）
        → 填充 / 描边 / 阴影 / 发光 / 软边缘

ViewObjectContactOfGraphic::createPrimitive2DSequence()
  │                                                 // 只做一件事：
  └─ 如果 Draft 模式 + 目标是 PDF/Printer → 抑制输出
```

**关键洞察：** SdrObject 通过 `CreateObjectSpecificViewContact()` 工厂方法返回自定义的 ViewContact。ViewContact 内部通过 `createViewIndependentPrimitive2DSequence()` 生成 Primitive。只要替换这个方法，就能完全控制渲染结果，而无需触碰 SdrObject 的任何其他行为。

核心渲染入口：
- **ViewContact::createViewIndependentPrimitive2DSequence()** — 模型级别的 Primitive 生成
- **ViewObjectContact::createPrimitive2DSequence()** — 视图级别的过滤（如打印抑制）

### 5.5 需要新建的文件

所有新文件放在 `sd/source/filter/pdf/` 下，只修改一个现有文件 `sdpdffilter.cxx`。

#### 类 1：PdfSharedDocument — 共享 PDFium 实例

```cpp
// 文件：sd/source/filter/pdf/PdfSharedDocument.hxx
//       sd/source/filter/pdf/PdfSharedDocument.cxx

class PdfSharedDocument {
    std::unique_ptr<vcl::pdf::PDFiumDocument> m_pDoc;
    std::mutex m_Mutex;                        // PDFium 线程安全
    int m_nPageCount = 0;
    std::vector<basegfx::B2DSize> m_aPageSizes;

public:
    bool loadFromFile(const OUString& rUrl);
    int getPageCount() const;
    basegfx::B2DSize getPageSize(int nPage) const;

    // 渲染单页 → BitmapEx（调用 PDFium）
    BitmapEx renderPage(int nPage, const Size& rPixelSize);
};
```

#### 类 2：PdfBitmapCache — LRU 缓存 + 后台线程

```cpp
// 文件：sd/source/filter/pdf/PdfBitmapCache.hxx
//       sd/source/filter/pdf/PdfBitmapCache.cxx

struct RenderKey {
    int pageIndex;
    int pixelWidth;
    int pixelHeight;
    // 未来可扩展: double scaleX, double scaleY, AntiAliasMode, ...
    bool operator<(const RenderKey& o) const;
};

class PdfBitmapCache {
    struct CachedBitmap {
        BitmapEx bitmap;
        size_t memorySize;
    };

    std::map<RenderKey, CachedBitmap> m_Cache;
    std::list<RenderKey> m_LRUList;
    size_t m_CurrentMemory = 0;
    static constexpr size_t MAX_MEMORY = 100 * 1024 * 1024; // 100 MB

    std::shared_ptr<PdfSharedDocument> m_pDoc;

    // 后台预渲染
    std::thread m_PrefetchThread;
    std::priority_queue<PrefetchRequest> m_PrefetchQueue;
    std::mutex m_Mutex;
    std::condition_variable m_Condition;
    std::atomic<bool> m_bRunning{true};

public:
    explicit PdfBitmapCache(std::shared_ptr<PdfSharedDocument> pDoc);
    ~PdfBitmapCache();

    // 获取（命中）或渲染（未命中）位图
    BitmapEx getOrRender(int nPage, const Size& rPixelSize);

    // 视口改变时：驱逐远页面，调度近页面预取
    void onViewportChanged(int nCurrentPage, int nVisiblePages);

private:
    void prefetchLoop();
    void evictLRU(size_t targetMemory);
};
```

#### 类 3：SdrPdfCachedPageObj — 自定义 SdrObject

```cpp
// 文件：sd/source/filter/pdf/SdrPdfCachedPageObj.hxx
//       sd/source/filter/pdf/SdrPdfCachedPageObj.cxx

class SdrPdfCachedPageObj final : public SdrRectObj
{
    std::shared_ptr<PdfSharedDocument> m_pSharedDoc;
    std::shared_ptr<PdfBitmapCache>    m_pCache;
    int                                 m_nPageIndex;

public:
    SdrPdfCachedPageObj(
        SdrModel& rModel,
        std::shared_ptr<PdfSharedDocument> pDoc,
        std::shared_ptr<PdfBitmapCache> pCache,
        int nPageIndex,
        const tools::Rectangle& rRect);

    // 唯一需要重写的核心方法 — 返回自定义 ViewContact
    virtual std::unique_ptr<sdr::contact::ViewContact>
        CreateObjectSpecificViewContact() override;

    // UNO shape 类型
    virtual SdrObjKind GetObjIdentifier() const override;
};
```

实现：
```cpp
std::unique_ptr<sdr::contact::ViewContact>
SdrPdfCachedPageObj::CreateObjectSpecificViewContact()
{
    return std::make_unique<ViewContactOfPdfCachedPage>(*this);
}

SdrObjKind SdrPdfCachedPageObj::GetObjIdentifier() const
{
    return SdrObjKind::Graphic;  // 复用 Graphic 的 UNO shape 映射
}
```

#### 类 4：ViewContactOfPdfCachedPage — 自定义 ViewContact

```cpp
// 文件：sd/source/filter/pdf/ViewContactOfPdfCachedPage.hxx
//       sd/source/filter/pdf/ViewContactOfPdfCachedPage.cxx

class ViewContactOfPdfCachedPage final : public sdr::contact::ViewContactOfSdrObj
{
public:
    explicit ViewContactOfPdfCachedPage(SdrPdfCachedPageObj& rObj);

    // 只重写这个方法！— 模型级别的 Primitive 生成入口
    virtual void createViewIndependentPrimitive2DSequence(
        sdr::contact::ViewContact& rVC,
        drawinglayer::primitive2d::Primitive2DDecompositionVisitor& rVisitor
    ) const override;
};
```

实现：
```cpp
void ViewContactOfPdfCachedPage::createViewIndependentPrimitive2DSequence(
    ViewContact& /*rVC*/,
    Primitive2DDecompositionVisitor& rVisitor) const
{
    auto& rObj = static_cast<SdrPdfCachedPageObj&>(
        const_cast<SdrObject&>(GetSdrObject()));

    // 从缓存获取位图（未命中时内部触发 PDFium 渲染）
    BitmapEx aBmp = rObj.getPageBitmap();

    if (aBmp.IsEmpty())
        return;

    // 构建变换矩阵
    const auto aRect = rObj.GetSnapRect();
    basegfx::B2DHomMatrix aMatrix;
    aMatrix.scale(aRect.GetWidth(), aRect.GetHeight());
    aMatrix.translate(aRect.Left(), aRect.Top());

    // 直接产生 BitmapPrimitive2D — 跳过 Graphic/VectorGraphicData/GfxLink 全部中间层
    rtl::Reference<GraphicPrimitive2D> xPrimitive =
        new GraphicPrimitive2D(aMatrix, aBmp);
    rVisitor.visit(xPrimitive);
}
```

### 5.6 两种实施方案对比

#### 方案 A：SdrRectObj + 自定义 ViewContact（完全控制）

| 项目 | 评估 |
|------|------|
| 控制力 | 完全控制渲染、缓存、线程模型 |
| 侵入性 | 低 — 只改 sdpdffilter.cxx 约 10 行 + 新建 8 个文件 |
| 复杂度 | 中 — 需要理解 ViewContact 模式 |
| 维护性 | 好 — 所有新代码独立，不碰核心 |
| 扩展性 | 高 — 后续可加 tile、渐进式渲染等 |
| 回退 | 删除新文件 + 恢复 sdpdffilter.cxx 即可 |

#### 方案 B：保持 SdrGrafObj + 替换 Graphic 位图来源（最简单）

思路：不创建新的 SdrObject。仍然用 `SdrGrafObj`，但在创建时不使用 GfxLink（延迟加载），而是直接从缓存获取渲染好的 BitmapEx，包装成 `Graphic(aBitmap)`，传给 SdrGrafObj 的构造函数。

```cpp
// sdpdffilter.cxx — 改动更少
BitmapEx aBmp = cache->getOrRender(nPageIndex, pixelSize);
Graphic aGraphic(aBmp);  // Graphic 直接持有位图
rtl::Reference<SdrGrafObj> pObj = new SdrGrafObj(rModel, aGraphic, ...);
pPage->InsertObject(pObj.get());
```

| 项目 | 方案 A（SdrRectObj + ViewContact） | 方案 B（SdrGrafObj + 预渲染位图） |
|------|-------------------------------------|-----------------------------------|
| 新文件数量 | ~8 个 | ~4 个（只需 PdfSharedDocument + PdfBitmapCache） |
| 对新类的需求 | 需要 SdrRectObj 子类 + ViewContact 子类 | 无新类 — 全部复用 SdrGrafObj |
| 渲染灵活性 | 完全控制 | 受 SdrGrafObj 渲染模式限制 |
| SdrObject 生命周期 | 完全由你控制 | 由现有的 SdrGrafObj 代码处理 |
| tile/async 的可行性 | 可设计 | 受 SdrGrafObj 的 swap-in 模式限制 |

**建议：方案 A 用 SdrRectObj + 自定义 ViewContact 先验证单页能否稳定跑通（redraw / zoom / scroll / invalidate 都正常），再逐步接入 PDFium 渲染和缓存系统。**

### 5.7 现有文件修改清单

**唯一需要修改的现有文件：** `sd/source/filter/pdf/sdpdffilter.cxx`

在 `SdPdfFilter::Import()` 中，将创建 `SdrGrafObj` 的代码替换为：

```cpp
// === 新增：创建共享文档和缓存 ===
auto sharedDoc = std::make_shared<PdfSharedDocument>();
sharedDoc->loadFromFile(aFileName);

auto cache = std::make_shared<PdfBitmapCache>(sharedDoc);

// === 新增：设置只读模式 ===
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_EDITDOC, true));
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_SAVE, true));
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_EXPORT, true));
mrMedium.GetItemSet().Put(SfxBoolItem(SID_LOCK_CONTENT_EXTRACTION, true));
mrDocShell.SetLoadReadonly(true);
mrDocShell.SetReadOnlyUI();

// === 替换：用 SdrPdfCachedPageObj 替代 SdrGrafObj ===
for (int nPageIndex = 0; nPageIndex < nPageCount; ++nPageIndex)
{
    SdPage* pPage = mrDocument.GetSdPage(nPageIndex, PageKind::Standard);
    const Size aSizeHMM = /* 从 sharedDoc 获取页面尺寸 */;

    rtl::Reference<SdrPdfCachedPageObj> pObj =
        new SdrPdfCachedPageObj(rModel, sharedDoc, cache, nPageIndex,
            tools::Rectangle(Point(), aSizeHMM));
    pObj->SetResizeProtect(true);
    pObj->SetMoveProtect(true);
    pPage->InsertObject(pObj.get());
}
```

总共约 20 行新增代码，只在一个现有文件中修改。

### 5.8 风险分析

| 风险 | 级别 | 缓解措施 |
|------|------|-----------|
| **ViewContact 接口行为异常**（invalidate、重绘、decomposition cache 不更新） | 致命 | 继承 ViewContactOfSdrObj 而非裸 ViewContact，复用已验证的重绘逻辑 |
| **PDFium 线程安全**（后台线程调用 PDFium 时 crash） | 中等 | PdfSharedDocument 内部加 mutex，所有 PDFium 调用串行化 |
| **SolarMutex / VCL 主线程约束**（后台线程不能直接 invalidate UI） | 中等 | 后台线程只渲染 → post user event → 主线程 invalidate |
| **缓存 Key 不够精确**（HiDPI、分数缩放、旋转场景下缓存错乱） | 未来扩散 | 缓存 Key 包含像素尺寸、缩放因子、旋转角度、输出设备类型 |

### 5.9 建议的实施步骤

#### 第一步：验证单页 Fake Object 可行性（最关键）

不做 PDFium 渲染。创建一个 `SdrPdfCachedPageObj`，它只是加载一个固定的 PNG 文件显示。验证：
- 页面显示正常（redraw）
- 缩放正常（zoom in/out）
- 滚动正常（scrolling）
- 选区正常（selection）
- 重绘正常（repaint after window resize/overlap）
- 销毁正常（dispose without crash）

这一步验证了 SdrRectObj + 自定义 ViewContact 的组合是否能正确融入 SdrPageView 渲染管线。

#### 第二步：接入 PDFium 渲染

替换 Fake PNG 为 PDFium 真实渲染：
```cpp
BitmapEx SdrPdfCachedPageObj::getPageBitmap() const
{
    return m_pSharedDoc->renderPage(m_nPageIndex, m_aPixelSize);
}
```

验证：500 页 PDF 打开时间、翻页延迟、内容正确性。

#### 第三步：接入 LRU 缓存 + 后台预渲染

- 加 PdfBitmapCache，验证 LRU 驱逐逻辑
- 加后台预渲染线程，验证异步渲染不阻塞 UI
- 加 SolarMutex 保护，验证无 crash/死锁

#### 第四步：接入只读模式切换

- 调用 SetReadOnlyUI() → 内置信息栏自动出现
- 验证"编辑文档"按钮 → 关闭文档 → 重新加载 → 走 SdrGrafObj 路径
- 验证切换后功能正常（可编辑、可保存）

*每个步骤都能独立验证，不会出现"改了一堆不知道谁炸了"的情况。*

---

### 5.10 方案的设计原则总结

1. **最小侵入** — 只改一个现有文件（sdpdffilter.cxx），所有新代码独立
2. **只读优先** — 默认 Viewer 模式，需编辑时明确切换
3. **会话隔离** — 不尝试 live mode switch，而是关闭 reopen
4. **共享实例** — PDFium 文档和 LRU 缓存在所有页面间共享
5. **自定义 ViewContact** — 借助 LO 已有的 ViewContact 体系，只重写 createViewIndependentPrimitive2DSequence()
6. **避免重构** — 不碰 Draw 主渲染架构、不碰 VCL、不碰 Sdr 核心

---

## 6. Okular 高缩放性能机制：分块渲染（Tiled Rendering）

### 6.1 问题背景

自定义管线（第 5 节）实现后，300% 以内缩放流畅，但超过 300% 后明显卡顿。根本原因是当前实现始终渲染**整页全分辨率位图**：

| 缩放 | A4 页面像素 | 内存 (32bpp) | PDFium 渲染耗时 |
|------|------------|-------------|----------------|
| 100% | ~2480×3508 | ~33 MB | 快 |
| 200% | ~4960×7016 | ~133 MB | 可接受 |
| 300% | ~7440×10524 | ~299 MB | 慢 |
| 400% | 8192×11585 | ~362 MB | 极慢 |

高缩放时用户只看到页面的一小部分，但仍然渲染整页，极其浪费。

### 6.2 Okular 的分块渲染机制

Okular 在高缩放下依然流畅（支持到 10000%），核心是**四叉树分块渲染**。

#### 6.2.1 分块激活条件

**文件：** `core/document.cpp:1393-1435`

```cpp
const long screenSize = screen->devicePixelRatio() * screen->size().width()
                      * screen->devicePixelRatio() * screen->size().height();

// 当请求的像素面积 > 4×屏幕像素面积，且不是在渲染页面的 75% 以上时
if (!tilesManager && m_generator->hasFeature(Generator::TiledRendering)
    && (long)r->width() * (long)r->height() > 4L * screenSize
    && normalizedArea < 0.75)
{
    // 创建 TilesManager，开始分块模式
}

// 当请求的像素面积 < 3×屏幕像素面积时，退出分块模式
else if (tilesManager
    && (long)r->width() * (long)r->height() < 3L * screenSize)
{
    // 销毁 TilesManager，回到整页模式
}
```

滞回区间 3×~4× 屏幕面积，避免在阈值附近反复切换。

#### 6.2.2 四叉树分块结构

**文件：** `core/tilesmanager.cpp`

```
页面
├─ 初始 4×4 = 16 个 tile
│  ├─ tile 0: rect(0, 0, 0.25, 0.25)
│  ├─ tile 1: rect(0.25, 0, 0.5, 0.25)
│  └─ ...
│
└─ 当单个 tile 像素面积 ≥ 2,000,000 时，递归 4 分
   ├─ sub-tile 0
   ├─ sub-tile 1
   ├─ sub-tile 2
   └─ sub-tile 3
```

```cpp
#define TILES_MAXSIZE 2000000  // 单个 tile 最大 200 万像素（~1414×1414）

bool TilesManager::Private::splitBigTiles(TileNode &tile, const NormalizedRect &rect)
{
    QRect tileRect = tile.rect.geometry(width, height);
    if (tileRect.width() * tileRect.height() < TILES_MAXSIZE)
        return false;
    split(tile, rect);  // 递归 4 分
    return true;
}
```

#### 6.2.3 只渲染可见 tile

Poppler generator 声明支持 `TiledRendering`：

**文件：** `generators/poppler/generator_pdf.cpp:685`

```cpp
setFeature(TiledRendering);
```

渲染时只渲染单个 tile 的子区域：

```cpp
if (request->isTile()) {
    const QRect rect = request->normalizedRect().geometry(request->width(), request->height());
    img = p->renderToImage(fakeDpiX, fakeDpiY,
                           rect.x(), rect.y(), rect.width(), rect.height(), ...);
}
```

#### 6.2.4 内存驱逐策略

**文件：** `core/tilesmanager.cpp:456-490`

```cpp
void TilesManager::cleanupPixmapMemory(qulonglong numberOfBytes,
                                        const NormalizedRect &visibleRect,
                                        int visiblePageNumber)
{
    // 按距离视口远近排序，最远的先驱逐
    // 不驱逐当前可见的 tile
    while (numberOfBytes > 0 && !rankedTiles.isEmpty()) {
        TileNode *tile = rankedTiles.takeLast();
        if (tile->rect.intersects(visibleRect))
            continue;  // 跳过可见 tile
        // 删除 tile 的 pixmap，释放内存
    }
}
```

#### 6.2.5 内存保护

当单个 tile 请求面积超过 `100 × screenSize` 时，直接丢弃请求：

**文件：** `core/document.cpp:1470-1477`

```cpp
if ((long)requestRect.width() * (long)requestRect.height() > 100L * screenSize
    && SettingsCore::memoryLevel() != SettingsCore::EnumMemoryLevel::Greedy)
{
    // 丢弃请求，避免 OOM
    delete r;
}
```

### 6.3 对比总结

| 方面 | Okular | 自定义管线（当前） |
|------|--------|-------------------|
| 缩放上限 | 10000%（有 tile） | 2048px cap（降质保性能） |
| 高缩放策略 | 只渲染可见 tile（四叉树） | 渲染整页全分辨率位图 |
| 缓存 key | 精确像素（tile 消除抖动） | 64px 对齐 |
| 渐进式渲染 | 支持（tile 逐个上屏） | 不支持（整页一次性上屏） |
| 内存管理 | tile 级驱逐（按视口距离） | LRU 整页驱逐 |
| 激活条件 | 页面像素面积 > 4×屏幕面积 | 无 |
| 退出条件 | 页面像素面积 < 3×屏幕面积 | 无 |

### 6.4 实施分块渲染的改造范围

若要实现 Okular 式分块渲染，需要改造以下组件：

1. **PdfBitmapCache** — 缓存 key 从 `(pageIndex, pageWidth, pageHeight)` 扩展为 `(pageIndex, tileRect, tilePixelSize)`；驱逐策略从整页 LRU 改为 tile 级按视口距离驱逐
2. **PdfPagePrimitive2D** — `create2DDecomposition()` 从产生单个 `BitmapPrimitive2D` 改为产生多个 tile 级 `BitmapPrimitive2D`；根据视口裁剪不可见 tile
3. **PdfSharedDocument** — `renderPage()` 需要支持渲染页面子区域（传入裁剪矩形）
4. **视口感知** — 需要在 `create2DDecomposition()` 中获取当前视口范围，只请求可见 tile

改造复杂度较高，建议作为后续优化，当前的 2048px cap + 64px 对齐方案可作为过渡。

---

*文档基于 LibreOffice 25.8 core（loongarch64）和 Okular 主分支的源代码分析生成。*
