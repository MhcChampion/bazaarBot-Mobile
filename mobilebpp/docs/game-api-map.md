# 大巴扎手机版 · Mod 开发 API 地图

> 数据源：`<project-root>\work\il2cpp-dump\dump.cs`（60.1 MB，约 145 万行，Il2CppDumper 全量 dump）
> 参考实现：`<project-root>\bpp\bazaarplusplus-mod\src`（BazaarPlusPlus，桌面版 BepInEx mod）
> 调查方式：`Select-String` 定点检索 + 按 `// TypeDefIndex: N` 定位类块；未整文件读入。
> 本文所有 `dump.cs:NNNN` 都是**该行号本身**（不是范围起点），可直接跳转核对。

## 约定

- dump 每行格式：`// RVA: 0x... Offset: 0x... VA: 0x...`。本 dump 中 **RVA == VA**（例如 `RVA: 0x74B99A4 ... VA: 0x74B99A4`），`Offset` 是文件偏移，写 hook 时不用它。
- 绝对地址 = 运行时模块基址（`TheBazaarRuntime.dll` / `GameAssembly.dll` 在内存中的 base）+ RVA。Il2CppInterop / MelonLoader 下通常直接用 `method.MethodInfo.MethodHandle.GetFunctionPointer()`，不必手算。
- 字段偏移：`// 0x10` 是**实例字段**偏移（对象头 0x10 起）；`// 0x0`、`// 0x8` 在本 dump 里出现于 **static 字段**（static 区从 0 起算）。
- 命名空间为空（`// Namespace:` 后面什么都没有）的类型在全局命名空间里，C# 里 `using` 不到，必须写全名或 `global::`。

---

## 0. 速查表

| 我想…… | 入口 | 详见 |
|---|---|---|
| 拿全部卡牌模板（图鉴） | `TheBazaar.Data.GetStatic()` → `JsonGameDataManager.GetCardMap()` | §1 |
| 按 GUID 查一张模板 | `JsonGameDataManager.GetCardById(Guid)` | §1.2 |
| 拿当前存档/对局 | `TheBazaar.Data.Run` / `TheBazaar.Data.CurrentState` | §2.1 |
| 拿玩家、手牌、仓库 | `Data.Run.Player.Hand` / `.Stash`（`IPlayerInventory`） | §2.3 |
| 拿棋盘 UI / socket 上的卡 | `Singleton<BoardManager>.Instance` | §2.4 |
| 找"当前玩家"最稳的运行时对象 | `Singleton<GameServiceManager>.Instance.GetPlayer()` | §2.2 |
| 实例卡的模板 GUID / 阶级 / 附魔 / 位置 | `ItemCard.TemplateId` / `.Tier` / `.Enchantment` / `.LeftSocketId` | §3 |
| 给 tooltip 追加文字 | `CardTooltipTypeHandler.RenderCardUI`（postfix） | §4.4 |
| 战斗加速 / 暂停 | `CombatSimHandler.SetSpeed` / `GameServiceManager.PauseOrUnpauseGame` | §5 |
| 新建 UGUI 面板 | `new GameObject(name)` + `AddComponent<Canvas>()`… | §6 |
| 读触摸 | `UnityEngine.Input.get_touchCount` / `GetTouch` | §6.7 |
| 每帧回调 | `EventSystem.Update` 或 `UpdateLoop.Update` | §7 |

---

## 1. 游戏静态数据（`JsonGameDataManager`）

### 1.1 它是不是单例？——**不是**

`JsonGameDataManager` 类型定义：`TheBazaar.DataManagement.Json`，TypeDefIndex **2704**。

```text
93855: public sealed class JsonGameDataManager // TypeDefIndex: 2704
93858: 	private static readonly BazaarJsonSerializerSettings SerializerSettings; // 0x0
93859: 	private static readonly JsonSerializerSettings SimpleSerializerSettings; // 0x8
93861: 	private static JsonSerializer _cardSerializer; // 0x80000000   [ThreadStatic]
93862: 	private readonly string _dbPath; // 0x10
93863: 	private readonly int _totalCardCount; // 0x18
93864: 	private Dictionary<Guid, ITCard> _cards; // 0x20
93866: 	private readonly Dictionary<Guid, TCollectible> _collectibles; // 0x30
93869: 	private readonly Dictionary<Guid, TMonster> _monsters; // 0x48
93871: 	private readonly Dictionary<string, TTooltipSymbol> _tooltips; // 0x58
93874: 	private static JsonSerializer CardSerializer { get; }        // 唯一的属性
93925: 	public static JsonGameDataManager Create() { }               // RVA 0x74B99A4
```

结论（逐条回答）：

| 问题 | 结论 | 证据 |
|---|---|---|
| 有 `static` 实例字段吗 | **没有**。3 个 static 字段全是序列化器/缓存，`_cardSerializer` 还是 `[ThreadStatic]` | dump.cs:93858-93861 |
| 有 `Instance` 属性吗 | **没有**。Properties 段只有一个 `private static JsonSerializer CardSerializer` | dump.cs:93874 |
| 是单例吗 | **不是**。没有任何自持有实例的成员 | 同上 |
| 怎么创建 | `public static JsonGameDataManager Create()`，RVA `0x74B99A4` | dump.cs:93925 |
| 谁持有实例 | `TheBazaar.Data` 的 `private static JsonGameDataManager manager; // 0xE8` | dump.cs:46753 |
| 谁调用 `Create()` | dump **没有调用图**，只能看到两个创建入口：`Data.CreateManager(bool, CancellationToken)`、`Data.CreateFallbackManager()`；`Data.EnsureGetStatic(CancellationToken)` 负责等待就绪 | dump.cs:47146 / 47150 / 47153 |

### 1.2 正确拿实例的调用链（推荐）

```csharp
// TheBazaar.Data : TypeDefIndex 1278  (dump.cs:46698)
if (!TheBazaar.Data.IsManagerCreated()) return;          // RVA 0x72EFD44  dump.cs:47142
var staticData = TheBazaar.Data.GetStatic();             // RVA 0x72EFCEC  dump.cs:47139
// 版本差异：某些构建里 GetStatic() 返回 Task<JsonGameDataManager>
var mgr = staticData as JsonGameDataManager
          ?? (staticData as Task<JsonGameDataManager>)?.Result;
```

`Data` 上与静态数据有关的成员：

| 成员 | 签名 | 偏移/备注 | RVA | dump.cs 行 |
|---|---|---|---|---|
| `manager` | `private static JsonGameDataManager` | static **0xE8** | — | 46753 |
| `GetStatic` | `public static JsonGameDataManager GetStatic()` | 返回 manager | `0x72EFCEC` | 47139 |
| `IsManagerCreated` | `public static bool IsManagerCreated()` | 非阻塞判断 | `0x72EFD44` | 47142 |
| `EnsureGetStatic` | `public static Task EnsureGetStatic(CancellationToken)` | async | `0x72EFDA4` | 47146 |
| `CreateManager` | `public static Task CreateManager(bool, CancellationToken)` | async，真正创建 | `0x72EFE7C` | 47150 |
| `CreateFallbackManager` | `public static void CreateFallbackManager()` | 失败兜底 | `0x72EFF6C` | 47153 |

> BPP 的同一套写法：`bpp/.../GameInterop/StaticCards/BppStaticDataAccess.cs:43-50`（先 `Data.IsManagerCreated()`，再 `Data.GetStatic()`，并显式处理 `Task<JsonGameDataManager>`）。

`JsonGameDataManager` 自己的可用读取方法：

| 方法 | 参数个数 | RVA | dump.cs 行 |
|---|---|---|---|
| `GetCardById(Guid)` → `ITCard` | 1 | `0x74B8F08` | 93886 |
| `GetCardMap()` → `Dictionary<Guid, ITCard>` | 0 | `0x74AE390` | 93890 |
| `GetMonsterById(Guid)` → `TMonster` | 1 | `0x74B9894` | 93914 |
| `GetCollectibleById(Guid)` → `TCollectible` | 1 | `0x74B977C` | 93902 |
| `GetTooltipById(string)` → `TTooltipSymbol` | 1 | `0x74B994C` | 93922 |
| `GetLevelUpById(int)` | 1 | `0x74B983C` | 93910 |
| `GetSeasonById(int)` | 1 | `0x74B98F4` | 93918 |
| `GetChallengeById(Guid)` | 1 | `0x74B9714` | 93894 |
| `GetGameModeById(Guid)` | 1 | `0x74B97DC` | 93906 |
| `Create()` | 0 | `0x74B99A4` | 93925 |
| `.ctor(string, int, Dictionary<Guid,TChallenge>, ..., Dictionary<string,TTooltipSymbol>)` | 9 | `0x74B8DC4` | 93882 |

`GetCardMap()` 首次调用很贵（整表 SQLite 读 + 多态 JSON 反序列化），BPP 明确把它放后台线程：`BppStaticDataAccess.cs:53-88`。

### 1.3 `ITCard` / `TMonster` / `TCollectible` 到底是什么类型

| 返回类型 | 全名 | 种类 | TypeDefIndex | dump.cs 行 |
|---|---|---|---|---|
| `ITCard` | `BazaarGameShared.Domain.Cards.ITCard` | **接口**（无字段，只有属性） | 9921 | 913372 |
| └ 实际运行时对象 | `BazaarGameShared.Domain.Cards.TCardBase`（抽象）→ `...Cards.Item.TCardItem` / `...Cards.TCardSkill` / `...Cards.TCardEncounter` … | class | 9927 / 9948 | 913746 / 915877 |
| `TMonster` | `BazaarGameShared.Domain.Players.TMonster` | sealed class | 9713 | 896555 |
| `TCollectible` | `BazaarGameShared.Domain.Collectibles.TCollectible` | sealed class | 9915 | 912729 |

`ITCard` 接口属性（**没有字段偏移**，接口不存数据；实现类见 §1.4）：

```text
913375: 	public abstract Guid Id { get; }
913377: 	public abstract string InternalName { get; }
913380: 	public abstract ECardType Type { get; }
913381: 	public abstract ECardSize Size { get; }
913382: 	public abstract ETier StartingTier { get; }
913383: 	public abstract HashSet<EHero> Heroes { get; }
913384: 	public abstract HashSet<ECardTag> Tags { get; }
913385: 	public abstract HashSet<EHiddenTag> HiddenTags { get; }
913386: 	public abstract string ArtKey { get; }
913391: 	public abstract TCardLocalization Localization { get; }
913392: 	public abstract Dictionary<string, TCardAbility> Abilities { get; }
913393: 	public abstract Dictionary<string, TCardAura> Auras { get; }
```

`TMonster` 字段（很小，怪物图鉴数据其实在 `Player` 里）：

| 字段 | 偏移 | dump.cs 行 |
|---|---|---|
| `<Id>k__BackingField` (Guid) | 0x10 | 896560 |
| `<Version>k__BackingField` (string) | 0x20 | 896563 |
| `<InternalName>k__BackingField` (string) | 0x28 | 896567 |
| `<Player>k__BackingField` (`TPlayer`) | 0x30 | 896570 |

`TCollectible` 字段：

| 字段 | 偏移 | dump.cs 行 |
|---|---|---|
| `Type` (string) | 0x10 | 912734 |
| `Id` (Guid) | 0x18 | 912737 |
| `Name` (string) | 0x28 | 912740 |
| `Notes` (string) | 0x30 | 912743 |
| `InternalName` (string) | 0x38 | 912746 |
| `Rarity` (string) | 0x40 | 912749 |
| `IsFoundInChests` (bool) | 0x48 | 912752 |
| `InStore` (bool) | 0x49 | 912755 |
| `HeroId` (string) | 0x50 | 912758 |
| `SeasonId` (string) | 0x58 | 912761 |

### 1.4 图鉴要用的字段偏移（`TCardBase` / `TCardItem`）

`TCardBase`（TypeDefIndex 9927，dump.cs:913746）：

| 字段 | 类型 | 偏移 | dump.cs 行 | 图鉴用途 |
|---|---|---|---|---|
| `<Id>k__BackingField` | Guid | 0x10 | 913751 | 模板 GUID |
| `<Version>k__BackingField` | string | 0x20 | 913754 | 数据版本 |
| `<InternalName>k__BackingField` | string | 0x28 | 913757 | 内部名（英文键） |
| `<InternalDescription>k__BackingField` | string | 0x30 | 913760 | 内部描述 |
| `<StartingTier>k__BackingField` | `ETier` | **0x38** | 913763 | **tier** |
| `<Size>k__BackingField` | `ECardSize` | **0x3C** | 913766 | **size** |
| `<Heroes>k__BackingField` | `HashSet<EHero>` | **0x40** | 913769 | **hero** |
| `<Tags>k__BackingField` | `HashSet<ECardTag>` | **0x48** | 913772 | **标签** |
| `<HiddenTags>k__BackingField` | `HashSet<EHiddenTag>` | 0x50 | 913775 | 隐藏标签 |
| `<ArtKey>k__BackingField` | string | 0x58 | 913778 | 立绘 key |
| `<SpawningEligibility>k__BackingField` | `ESpawnEligibility` | 0x60 | 913781 | 掉落限制 |
| `<TranslationKey>k__BackingField` | string | 0x68 | 913784 | 已废弃 |
| `<AudioKey>k__BackingField` | string | 0x70 | 913787 | 音效 |
| `<Localization>k__BackingField` | `TCardLocalization` | **0x78** | 913790 | **名称/描述** |
| `<Abilities>k__BackingField` | `Dictionary<string,TCardAbility>` | 0x80 | 913793 | 技能 |
| `<Auras>k__BackingField` | `Dictionary<string,TCardAura>` | 0x88 | 913796 | 光环 |

`TCardItem`（**物品类模板，图鉴 99% 用这个**；TypeDefIndex 9948，dump.cs:915877；命名空间 `BazaarGameShared.Domain.Cards.Item`）追加字段：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `<Tiers>k__BackingField` | `Dictionary<ETier, TCardTier>` | **0x90** | 915882 |
| `<Enchantments>k__BackingField` | `Dictionary<EEnchantmentType, TEnchantment>` | 0x98 | 915886 |
| `<Transform>k__BackingField` | `TCardTransform` | 0xA0 | 915890 |
| `<Quests>k__BackingField` | `List<TQuestGroup>` | 0xA8 | 915894 |
| `<Type>k__BackingField` | `ECardType` | **0xB0** | 915897 |

`TCardLocalization`（TypeDefIndex 9929，dump.cs:914140）：

| 字段 | 类型 | 偏移 | dump.cs 行 | 用途 |
|---|---|---|---|---|
| `<Title>k__BackingField` | `TLocalizableText` | 0x10 | 914145 | **名称** |
| `<Description>k__BackingField` | `TLocalizableText` | 0x18 | 914149 | **描述** |
| `<FlavorText>k__BackingField` | `TLocalizableText` | 0x20 | 914153 | 风味文本 |
| `<Tooltips>k__BackingField` | `List<TTooltip>` | 0x28 | 914156 | 富文本 tooltip |

`TLocalizableText`（TypeDefIndex 9981，dump.cs:918155，命名空间 `BazaarGameShared.Domain.Core`）：

| 字段 | 偏移 | dump.cs 行 |
|---|---|---|
| `<Key>k__BackingField` (string) | 0x10 | 918160 |
| `<Text>k__BackingField` (string) | 0x18 | 918163 |

`TCardTier`（TypeDefIndex 9930，dump.cs:914257）：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `<Attributes>k__BackingField` | `Dictionary<ECardAttributeType,int>` | **0x10** | 914262 |
| `<AbilityIds>k__BackingField` | `HashSet<string>` | 0x18 | 914265 |
| `<AuraIds>k__BackingField` | `HashSet<string>` | 0x20 | 914268 |
| `<TooltipIds>k__BackingField` | `List<int>` | 0x28 | 914271 |

**价格 / 冷却 / 伤害等数字都在 `TCardTier.Attributes` 字典里**，键是 `ECardAttributeType`（dump.cs:918223）：

| 枚举值 | 数值 | dump.cs 行 |
|---|---|---|
| `Cooldown` | **4** | 918231 |
| `CooldownMax` | 5 | 918232 |
| `DamageAmount` | 27 | 918251 |
| `HealAmount` | 29 | 918253 |
| `ShieldApplyAmount` | 34 | 918258 |
| **`BuyPrice`** | **37** | 918261 |
| **`SellPrice`** | **38** | 918262 |
| `Multicast` | 24 | 918248 |
| `CritChance` | 26 | 918250 |
| `Ammo` / `AmmoMax` | 0 / 1 | 918227 / 918228 |
| `TempoCost` | 101 | 918313 |

取法（伪代码）：

```csharp
var item = tpl as TCardItem;                        // 0x0 -> TCardBase
var title = item.Localization.Title.Text;           // 0x78 -> 0x10 -> 0x18
var desc  = item.Localization.Description?.Text;    // 0x78 -> 0x18 -> 0x18
var tier  = item.StartingTier;                      // 0x38
var size  = item.Size;                              // 0x3C
var heroes= item.Heroes;                            // 0x40
var tags  = item.Tags;                              // 0x48
var type  = item.Type;                              // 0xB0 (TCardItem 覆写)
if (item.Tiers.TryGetValue(tier, out var t)) {
    int price    = t.Attributes[(ECardAttributeType)37];  // BuyPrice
    int cooldown = t.Attributes[(ECardAttributeType)4];   // Cooldown
}
```

其余枚举速查（都用于图鉴筛选/文案）：

| 枚举 | 取值 | dump.cs 行 |
|---|---|---|
| `ETier` | Bronze 0, Silver 1, Gold 2, Diamond 3, Legendary 4 | 918951 |
| `ECardSize` | Small 1, Medium 2, Large 3 | 918342 |
| `ECardType` | Item 0, Skill 1, CombatEncounter 2, EncounterStep 3, EventEncounter 4, PedestalEncounter 5, PvpEncounter 6, SocketEffect 7, PlayerEffect 8 | 918421 |
| `EHero` | Common 0, Pygmalien 1, Vanessa 2, Stelle 3, Jules 4, Dooley 5, Mak 6, Karnok 7, Hero8 8 | 918593 |
| `ECardTag` | Weapon 0 … Instrument 29 | 918384 |
| `EHiddenTag` | Damage 0 … | 918609 |
| `ECardState` | Alive 0, Casting 1, Disabled 2, Transformed 3 | 918352 |
| `EInventorySection` | Hand 0, Stash 1 | 918714 |
| `ECombatantId` | Player 0 … | 918446 |

### 1.5 本节的"未找到"

- `JsonGameDataManager` **没有** `Instance` 静态字段 / `Instance` 属性 / `GetInstance()` 方法（dump.cs:93855-93995 全块无此成员）。
- dump 里**没有调用关系**，因此"谁调用了 `JsonGameDataManager.Create()`"无法从 dump 直接证明；只能给出 `Data.CreateManager` / `Data.CreateFallbackManager` 这两个唯一的创建入口，以及 BPP 的运行时用法。

---

## 2. 当前对局 / 棋盘 / 商店 / 仓库

### 2.1 `TheBazaar.Data`：所有"当前状态"的静态总入口

`public class Data`（TypeDefIndex **1278**，dump.cs:46698）。它是一堆静态字段的持有者，等同于"客户端全局状态"。

| 成员 | 类型 | static 偏移 | getter RVA | dump.cs 行 |
|---|---|---|---|---|
| `Run` | `Run` | 0x10 | `0x72EA4C0` | 46706 / 46858 |
| `CurrentState` | `RunState` | 0x18 | `0x72EA578` | 46708 / 46866 |
| `Entities` | `Dictionary<InstanceId, Card>` | **0x28** | 直接读字段 | 46711 |
| `SkillSockets` | `Dictionary<Card, BazaarBoard.ESkillSocket>` | 0x30 | — | 46712 |
| `CurrentEncounterId` | `Nullable<Guid>` | 0x38 | `0x72EA6E8` | 46714 |
| `CurrentBoardId` | `string` | 0x50 | `0x72EA884` | 46718 |
| `HasActiveRun` | `bool`（只读属性） | — | `0x72EA3B4` | 46790 / 46851 |
| `IsInCombat` | `bool`（只读属性） | — | `0x72EA9F4` | 46798 / 46913 |
| `IsInPostCombat` | `bool` | — | `0x72EAAC8` | 46916 |
| `SelectedHero` | `EHero` | — | `0x72EAC2C` | 46919 |
| `Updater` | `IUpdateLoop` | 0x68 | `0x72ED938` | 46725 / 46970 |
| `manager` | `JsonGameDataManager` | 0xE8 | — | 46753 |

`InstanceId`（`BazaarGameShared.Domain.Core`，TypeDefIndex 9980，dump.cs:918086）是 `string` 包装的结构体，`Value` 在 0x0（dump.cs:918087）——`Data.Entities` 的键就是它。

按阵营/背包取卡牌的通用方法（泛型，注意泛型实例化 RVA）：

```text
46946: 	public static List<T> GetCards<T>(ECombatantId combatantId, Nullable<EInventorySection> section) { }
46949: 	|-RVA: 0x45D1810 ... |-Data.GetCards<object>
```

### 2.2 `AppState` 与 `GameServiceManager`：两条 UI/运行时入口

**(a) `AppState`（TypeDefIndex 1374，dump.cs:50806）——状态机**

| 成员 | 签名 | 偏移 | RVA | dump.cs 行 |
|---|---|---|---|---|
| `CurrentState` | `public static AppState CurrentState { get; }` | static 0x0 | `0x730D2F8` | 50810 / 50861 |
| `PreviousState` | `public static AppState PreviousState { get; }` | static 0x8 | `0x730D3B8` | 50812 / 50869 |
| `BlockInput` | `public static bool BlockInput { get; }` | static 0x11 | `0x730D528` | 50885 |
| `GetState<T>` | `public static T GetState<T>()` | — | 泛型实例 `0x40B8170`（`GetState<object>`） | 50965 |
| `BuyItemCommand` | `public bool BuyItemCommand(ItemCard, EInventorySection, bool)` | — | `0x730FD1C` | 51043 |
| `BuyItemCommand` | `public bool BuyItemCommand(ItemCard, List<EContainerSocketId>, EInventorySection, bool)` | — | `0x730FD6C` | 51046 |
| `SellCardCommand` | `public virtual void SellCardCommand(ItemCard)` | — | `0x730FE84` | 51049 |
| `MoveCardCommand` | `public void MoveCardCommand(ItemCard, List<EContainerSocketId>, EInventorySection)` | — | `0x730FFF4` | 51052 |
| `RerollCommand` | `public virtual bool RerollCommand()` | — | `0x73100F8` | 51055 |
| `SelectEncounterCommand` | `public bool SelectEncounterCommand(InstanceId)` | — | `0x730F8FC` | 51037 |
| `SelectSkillCommand` | `public virtual bool SelectSkillCommand(SkillCard, Nullable<BazaarBoard.ESkillSocket>)` | — | `0x730FB90` | 51040 |

具体状态子类（`AppState.CurrentState is X` 判断）：

| 状态 | 父类 | TypeDefIndex | dump.cs 行 |
|---|---|---|---|
| `ChoiceState` | `RunAppState` | 1377 | 51183 |
| `CombatState` | `RunAppState` | 1380 | 51242 |
| `EncounterState`（商人/事件都走这里） | `RunAppState` | 1382 | 51279 |
| `LevelUpState` | `RunAppState` | 1388 | 51405 |
| `LootState` | `RunAppState` | 1389 | 51427 |
| `PedestalState` | `RunAppState` | 1395 | 51586 |
| `ReplayState` | `AppState` | 1405 | 51865 |
| `StartRunAppState` | `AppState` | 1417 | 52220 |
| `EndRunVictoryState` / `EndRunDefeatState` | `RunAppState` | 1386 / 1384 | 51368 / 51319 |

**(b) `GameServiceManager : Singleton<GameServiceManager>`（全局命名空间，TypeDefIndex 360，dump.cs:14613）——运行时对象总仓**

> 这是 BPP 自己也用的入口（`bpp/.../Game/CombatReplay/Bootstrap/BootstrapManagerInitializer.cs:17`、`ReplayBootstrap.cs:241-242`）。

| 成员 | 类型/签名 | 偏移 | RVA | dump.cs 行 |
|---|---|---|---|---|
| `Instance` | 继承自 `Singleton<T>` 的 **public static T** | static 0x0 | — | 17954（`Singleton<T>`） |
| `boardManager` | `BoardManager` | 0x38 | — | 14620 |
| `playerBoardUI` | `BoardUIController` | 0x70 | — | 14631 |
| `opponentBoardUI` | `BoardUIController` | 0x78 | — | 14633 |
| `GamePaused` | `public bool GamePaused { get; }` | 0x120 | `0x764AE68` | 14666 / 14697 |
| `GetPlayer()` | `public Player GetPlayer()` | — | `0x764B6C8` | 14745 |
| `GetOpponent()` | `public Player GetOpponent()` | — | `0x764B7C8` | 14752 |
| `GetBazaarBoard()` | `public BazaarBoard GetBazaarBoard()` | — | `0x764B5A4` | 14734 |
| `GetBazaarBoardAsync()` | `public Task<BazaarBoard> GetBazaarBoardAsync()` | — | `0x764B5C0` | 14738 |
| `PauseOrUnpauseGame` | `public void PauseOrUnpauseGame(bool)` | — | **`0x764C1C0`** | 14795 |
| `StartOrReverseTimeLerp` | `public void StartOrReverseTimeLerp(bool)` | — | `0x764C5A4` | 14801 |
| `EnforceMaxTimeScale` | `public void EnforceMaxTimeScale(float)` | — | `0x764C834` | 14807 |
| `GamePauseHandler` | `public static Task GamePauseHandler(Nullable<CancellationToken>)` | — | `0x764C844` | 14811 |
| `IsCardPurchaseValid` | `public EPurchaseCheckResult IsCardPurchaseValid(BazaarCard)` | — | `0x764BAF8` | 14777 |
| `SetUpBoard` | `public bool SetUpBoard(EHero)` | — | `0x764BA18` | 14762 |
| `NormalSpeedTimeScaleValue` | `float`（正常 timeScale 缓存） | 0xAC | — | 14644 |
| `IsTimeLerping` | `bool` | 0xB0 | — | 14646 |
| `GamePausedOrUnpaused` | `Action<bool>` 事件 | 0x128 | — | 14667 |

### 2.3 逻辑棋盘 / 仓库：`Run` → `Player` → 背包

`Run`（`BazaarGameClient.Domain.Models`，TypeDefIndex 22404，dump.cs:1607858）：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `GameModeId` | Guid | 0x10 | 1607863 |
| `DataVersion` | Version | 0x20 | 1607866 |
| `Hour` / `Day` | uint | 0x2C / 0x30 | 1607872 / 1607875 |
| `Victories` / `Losses` | uint | 0x34 / 0x38 | 1607878 / 1607881 |
| **`Player`** | `Player` | **0x40** | 1607884 |
| **`Opponent`** | `Player` | **0x48** | 1607888 |
| `SeedManager` | `ISeedManager` | 0x50 | 1607891 |
| `PlayMode` | `EPlayMode` | 0x58 | 1607894 |

`Player`（TypeDefIndex 22403，dump.cs:1607728）：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `SocketCount` | **static int** | static 0x0 | 1607731 |
| `CombatantId` | `ECombatantId` | 0x10 | 1607734 |
| `Attributes` | `Dictionary<EPlayerAttributeType,int>` | 0x18 | 1607737 |
| `Hero` | `EHero` | 0x20 | 1607740 |
| **`Hand`** | `IPlayerInventory` | **0x28** | 1607743 |
| **`Stash`** | `IPlayerInventory` | **0x30** | 1607746 |
| `Skills` | `List<SkillCard>` | 0x38 | 1607749 |
| **`Socket`** | `IPlayerInventory` | **0x40** | 1607752 |
| `SocketEffects` | `IPlayerInventory` | 0x48 | 1607755 |

`IPlayerInventory`（TypeDefIndex 9711，dump.cs:896395）：

```text
896398: 	public abstract SocketedContainer Container { get; set; }   // slot 0/1
896421: 	public abstract List<ICard> GetItems();                    // slot 6
896424: 	public abstract IEnumerable<ICard> GetItemsAsEnumerable(); // slot 7
```

`SocketedContainer`（TypeDefIndex 9923，dump.cs:913468）：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `SocketCount` | **static readonly int** | static 0x0 | 913471 |
| `<Sockets>k__BackingField` | `ISocketable[]` | **0x10** | 913475 |
| `UnlockedSockets` | ushort（位掩码） | 0x18 | 913476 |

`EContainerSocketId`：`Socket_0 = 0 … Socket_9 = 9`（dump.cs:918503）。

`RunState`（TypeDefIndex **22405**，dump.cs:1608041）其实很薄——只有"当前遭遇/选择"信息：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `StateName` | `ERunState` | 0x10 | 1608044 |
| `CurrentEncounterId` | string | 0x18 | 1608047 |
| `RerollCost` | `Nullable<uint>` | 0x20 | 1608050 |
| `RerollsRemaining` | `Nullable<uint>` | 0x28 | 1608053 |
| **`SelectionSet`** | `List<string>`（当前可选卡 instanceId） | **0x30** | 1608057 |
| `SelectionContextRules` | `TSelectionContextRules` | 0x38 | 1608060 |
| 方法 `GetCardsInSelectionSet()` | `IEnumerable<ICard>` | — | 1608116 |
| 方法 `GetCurrentEncounter()` | `ICard` | — | 1608119 |

### 2.4 棋盘 UI 层：`BoardManager`

`public sealed class BoardManager : Singleton<BoardManager>`（**全局命名空间**，TypeDefIndex 348，dump.cs:13245）：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `playerItemSockets` | `ItemSocketController[]` | 0x60 | 13291 |
| `playerStorageSockets` | `ItemSocketController[]` | 0x68 | 13292 |
| `opponentItemSockets` | `ItemSocketController[]` | 0x70 | 13293 |
| `playerSkillSockets` | `SkillSocketController[]` | 0x78 | 13294 |
| `opponentSkillSockets` | `SkillSocketController[]` | 0x80 | 13295 |
| `BoardStatePresentation` | `BoardStatePresentation` | 0x38 | 13268 |
| `merchantFrame` | `GameObject` | 0x48 | 13272 |
| `battleFrame` | `GameObject` | 0x50 | 13274 |
| `IsInitialized` | bool | 0x30 | 13264 |
| `IsUpdatingBoard` | bool | 0x31 | 13266 |
| `IsRecapViewOpen` / `IsBoardFlipped` | bool | 0x58 / 0x59 | 13276 / 13278 |
| `StorageMoving` | bool | 0x5D | 13286 |

`Singleton<T>`（TypeDefIndex 472，dump.cs:17951）：

```text
17954: 	public static T Instance; // 0x0
```

所以：`Singleton<BoardManager>.Instance`、`Singleton<GameServiceManager>.Instance` 都是静态字段读取（BPP 写法：`bpp/.../Bootstrap/BootstrapManagerInitializer.cs:21`）。

从 UI 回调到数据模型：

| 类型 | 字段 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `ItemSocketController`（TypeDefIndex 1159） | `SocketNumber` (`EContainerSocketId`) | 0x38 | 42181 |
| `CardController`（TypeDefIndex 109） | `<CardData>k__BackingField` (`Card`) | **0x90** | 3761 |
| `CardController` | `boardSection` (`BazaarBoard.ESections`) | 0x98 | 3762 |

`BazaarBoard`（`BazaarBattleService.Models`，TypeDefIndex 14377，dump.cs:1157871）是战斗表现用的模型（数组里是 `BazaarCard`），不是逻辑棋盘：

| 字段 | 偏移 | dump.cs 行 |
|---|---|---|
| `CarpetUnlocks` (`uint[]`) | 0x48 | 1157876 |
| `OpponentCardHand` (`BazaarCard[]`) | 0x50 | 1157879 |
| `PlayerCardHand` (`BazaarCard[]`) | 0x58 | 1157882 |
| `PlayerStorageHand` (`BazaarCard[]`) | 0x60 | 1157885 |
| `OpponentSkillCard` | 0x68 | 1157888 |
| `PlayerSkillCard` | 0x70 | 1157891 |
| `Opponent` (`BazaarCard`) | 0x78 | 1157892 |
| `Toys` (`ulong[]`) | 0x80 | 1157895 |
| `boardStatus` | 0x88 | 1157896 |

### 2.5 商店（shop）

**dump 里没有 `ShopState` / `ShopInventory` / `ShopController` 类型**（`ShopState` 全文 0 命中；`Shop*` 只命中 `ShopTransactionModel`，TypeDefIndex 14509，dump.cs:1164717，那是充值/内购交易模型）。

实际结构：

- 游戏内"商店"是一次 **Merchant 遭遇**，走 `EncounterState`（dump.cs:51279）；`MerchantSO`（dump.cs:17599）只有美术资源字段（`merchantImageBG` 0x50、`merchantImage` 0x58、`parentPortrait` 0x60），没有货架数据。
- 货架上的卡就是棋盘上/遭遇里的 `ItemCard`，购买命令在 `AppState`：
  - `BuyItemCommand(ItemCard, EInventorySection, bool)` RVA `0x730FD1C`（dump.cs:51043）
  - `BuyItemCommand(ItemCard, List<EContainerSocketId>, EInventorySection, bool)` RVA `0x730FD6C`（dump.cs:51046）
  - `RerollCommand()` RVA `0x73100F8`（dump.cs:51055）
- 购买合法性：`GameServiceManager.IsCardPurchaseValid(BazaarCard)` RVA `0x764BAF8`（dump.cs:14777）。
- 元游戏商店（皮肤/礼包）用 `StoreMediator`（TypeDefIndex 2159，dump.cs:76163），与对局内商店无关。

> 结论：想做"商店增强"，正确做法是遍历当前遭遇/棋盘上的 `ItemCard`（§2.3/§2.4），而不是找 `ShopState`。

### 2.6 BPP 是怎么拿这些对象的

| BPP 位置 | 做法 | 对应 dump |
|---|---|---|
| `GameInterop/GameStateProbe.cs:14-21` | `AppState.CurrentState is RunAppState / ReplayState / StartRunAppState` 判断"是否在局内" | dump.cs:50861 |
| `Game/CollectionPanel/CollectionPanel.cs:255,275` | `TheBazaar.Data.Run?.Player?.Hero`、`.Day` | dump.cs:46706 / 1607884 |
| `Game/CombatReplay/Bootstrap/AppStateHandlerInstaller.cs:49-50` | `Data.Run?.Player?.Skills`、`Data.Run?.Opponent?.Skills` | dump.cs:1607749 |
| `Game/CombatReplay/Bootstrap/ReplayOpeningStateRestorer.cs:389` | `Data.Entities[rawCard.InstanceId] = rawCard` | dump.cs:46711 |
| `Game/MusicNotes/NativeMusicNotePreviewController.cs:41` | `Data.Run?.Player?.Socket?.Container` | dump.cs:1607752 / 913475 |
| `Game/CombatReplay/Bootstrap/BootstrapManagerInitializer.cs:17-21` | `Singleton<GameServiceManager>.Instance`、`Singleton<BoardManager>.Instance` | dump.cs:17954 / 14613 |
| `Game/CombatReplay/CombatReplayRuntime.cs:469,1027-1032` | `boardManager.IsRecapViewOpen / StorageMoving / IsUpdatingBoard / HasCardsToReveal()` | dump.cs:13245 起 |
| `GameInterop/StaticCards/BppStaticDataAccess.cs:43-50` | `Data.IsManagerCreated()` + `Data.GetStatic()` | dump.cs:47139 |

### 2.7 可靠性排序（结论）

1. **`Singleton<GameServiceManager>.Instance.GetPlayer()`** — 最稳：单例、显式 API、BPP 已在用；同一对象上还有 `GetBazaarBoard()` / `GetOpponent()`。
2. **`TheBazaar.Data.Run` / `Data.CurrentState`** — 纯静态字段，零查找成本，但需要判空（非局内为 null）。
3. `Singleton<BoardManager>.Instance` — 只在局内、棋盘加载后有效（`IsInitialized`）。
4. `Data.Entities` — 全量实体字典，适合做索引，不适合每帧遍历。

---

## 3. 卡牌实例（`ItemCard`）

### 3.1 继承链

```text
1608474: public class Card : ICard, ISocketable // TypeDefIndex: 22413
1608868: public class ItemCard : Card, IUpgradeable, IFuseable<ItemCard>, IFuseable, IEnchantable // TypeDefIndex: 22424
```

即：`ItemCard : Card : ICard, ISocketable`（`ItemCard` 只多一个字段 `Enchantment`）。
命名空间：`BazaarGameClient.Domain.Models.Cards`。

### 3.2 关键字段偏移

`Card`（TypeDefIndex 22413，dump.cs:1608474）：

| 字段 | 类型 | 偏移 | dump.cs 行 | 说明 |
|---|---|---|---|---|
| `<InstanceId>k__BackingField` | `InstanceId`（string 包装） | 0x10 | 1608479 | 实例 id |
| **`<TemplateId>k__BackingField`** | **Guid** | **0x18** | 1608482 | 模板 GUID |
| `<Attributes>k__BackingField` | `Dictionary<ECardAttributeType,int>` | **0x28** | 1608485 | **当前**属性（含加成） |
| `<Heroes>k__BackingField` | `HashSet<EHero>` | 0x30 | 1608488 | |
| `<HiddenTags>k__BackingField` | `HashSet<EHiddenTag>` | 0x38 | 1608491 | |
| **`<Size>k__BackingField`** | `ECardSize` | **0x40** | 1608494 | |
| `<Tags>k__BackingField` | `HashSet<ECardTag>` | 0x48 | 1608497 | |
| **`<Tier>k__BackingField`** | `ETier` | **0x50** | 1608500 | 当前阶级 |
| `<Type>k__BackingField` | `ECardType` | 0x54 | 1608503 | |
| `<Owner>k__BackingField` | `IPlayer` | 0x58 | 1608507 | 归属（玩家/对手） |
| **`<LeftSocketId>k__BackingField`** | `Nullable<EContainerSocketId>` | **0x60** | 1608510 | **socket 位置** |
| **`<Section>k__BackingField`** | `Nullable<EInventorySection>` | **0x68** | 1608513 | Hand / Stash |
| `<State>k__BackingField` | `ECardState` | 0x70 | 1608516 | |
| **`<Template>k__BackingField`** | `ITCard` | **0x78** | 1608520 | 缓存好的模板对象 |

`ItemCard`（TypeDefIndex 22424，dump.cs:1608868）：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| **`<Enchantment>k__BackingField`** | `Nullable<EEnchantmentType>` | **0x80** | 1608873 |

实例上的便捷方法（不用手读偏移）：

| 方法 | 参数个数 | RVA | dump.cs 行 |
|---|---|---|---|
| `GetAttributeValue(ECardAttributeType)` | 1 | `0x3E917C0` | 1608673 |
| `GetUnscaledAttributeValue(ECardAttributeType)` | 1 | `0x3E91864` | 1608676 |
| `TryGetAttributeValue(ECardAttributeType, out Nullable<int>)` | 2 | `0x3E9056C` | 1608679 |
| `GetNextTier()` | 0 | `0x3E91868` | 1608682 |
| `GetTemplate()` | 0 | `0x3E917B0` | 1608667 |
| `GetSocketId()` | 0 | `0x3E917B8` | 1608670 |
| `GetActiveAbilities()` | 0 | `0x3E91878` | 1608685 |
| `ItemCard.CanUpgrade()` | 0 | `0x3E95228` | 1608896 |
| `ItemCard.Upgrade(Nullable<ETier>, Nullable<EEnchantmentType>, bool, bool)` | 4 | `0x3E95238` | 1608899 |
| `ItemCard.GetEnchantments()` | 0 | `0x3E9517C` | 1608889 |

### 3.3 TemplateId → 静态定义

三条路径，按可靠性排序：

```csharp
// (1) 实例已缓存模板指针（最快，0x78）
var tpl = itemCard.Template as TCardItem;         // Card.Template, 0x78

// (2) 用 TemplateId 查静态库（最稳，跨实例复用）
var mgr = TheBazaar.Data.GetStatic() as JsonGameDataManager;
var tpl2 = mgr.GetCardById(itemCard.TemplateId) as TCardItem;   // 0x18 -> RVA 0x74B8F08

// (3) 自己建索引（图鉴/批量扫描）
var map = mgr.GetCardMap();                       // RVA 0x74AE390
```

BPP 的封装就是 (2)：`bpp/.../GameInterop/StaticCards/BppStaticDataAccess.cs:27-33`（`manager.GetCardById(templateId) as TCardBase`）。

注意：`Template`（0x78）可能是 null（未反序列化的占位卡），(2) 永远可用。

### 3.4 实例卡上的"价格/冷却"

- 实例的**当前**值：`Card.Attributes`（0x28）里 `ECardAttributeType.BuyPrice(37)` / `SellPrice(38)` / `Cooldown(4)`。
- 模板的**基础**值：`TCardItem.Tiers[tier].Attributes`（0x90 → `TCardTier.Attributes` 0x10）。
- 实例的 tier：`Card.Tier`（0x50）；附魔：`ItemCard.Enchantment`（0x80）。

---

## 4. Tooltip

### 4.1 对象关系

```text
CardTooltipData (TheBazaar.Tooltips, 2813)        // 纯数据/文本生成
        ↓ 被读取
CardTooltipController (TheBazaar.UI.Tooltips, 3490) // 面板/布局/生命周期
        ↓ 内部持有
CardEffectTooltipController (3483)                 // 一行文字 + bullet
        ↓ textObject (TMP_Text, 0x50)
TMPro.TMP_Text.text
        ↑
BaseTooltipController.headerText (TMP_Text, 0x30)  // 标题
```

### 4.2 `CardTooltipData`（TypeDefIndex 2813，dump.cs:97487）——生成文本

字段：

| 字段 | 类型 | 偏移 | dump.cs 行 |
|---|---|---|---|
| `_cardInstance` | `Card` | 0x10 | 97506 |
| `_cardTemplate` | `ITCard` | 0x18 | 97507 |
| `_valueContext` | `ValueContext` | 0x20 | 97508 |
| `_compiledTooltips` | `Dictionary<string, TooltipBuilder>` | 0x40 | 97510 |
| `_monster` | `TMonster` | 0x50 | 97513 |

方法（全部是"文本生成"，返回 string / segment / builder）：

| 方法 | 参数个数 | RVA | dump.cs 行 |
|---|---|---|---|
| `GetTitle()` → string | 0 | `0x74DC6D8` | 97564 |
| `GetCardDescription()` → `(string, Nullable<TooltipSegment>)` | 0 | `0x74DC900` | 97566 |
| `GetPassiveTooltipBlock(Card)` → `(StringBuilder, Nullable<TooltipSegment>)` | 1 | `0x74DFC98` | 97585 |
| `GetActiveAbilityTooltipBlock()` → `List<TooltipSegment>` | 0 | `0x74DF1D4` | 97582 |
| `BuildCombinedEffectDescription()` → string | 0 | `0x74E0928` | 97587 |
| `GetLegendTooltips(bool)` → `List<LegendTooltip>` | 1 | `0x74DD408` | 97573 |
| `GetAttributeModifications(ECardAttributeType, float)` → StringBuilder | 2 | `0x74E1B48` | 97600 |
| `CreateCardTooltipData(Card)`（静态工厂） | 1 | `0x74DBB84` | 97546 |
| `.ctor(Card, ITCard, TMonster)` | 3 | `0x74DBD84` | 97549 |

### 4.3 `CardTooltipController`（TypeDefIndex 3490，dump.cs:122199）——渲染 / 写文本

| 方法 | 参数个数 | RVA | dump.cs 行 | 作用 |
|---|---|---|---|---|
| `ShowTooltipController(Transform, Vector3, ITooltipData, bool, CardTooltipController)` | 5 | `0x75E3A48` | 123052 | 显示入口 |
| `UpdatePassiveEffects(Card, string)` | 2 | `0x75E42D8` | 123070 | **写被动效果文本** |
| `RenderPassiveEffectTextBlock(string, List<CardQuestGroupData>)` | 2 | `0x75E42F8` | 123073 | **渲染被动段落（BPP 最常用 hook 点）** |
| `UpdateActiveEffects(Card, in List<TooltipSegment>)` | 2 | `0x75E43B4` | 123076 | **写主动效果文本** |
| `RenderActiveEffectsTextBlock(in List<TooltipSegment>, List<CardQuestGroupData>)` | 2 | `0x75E4510` | 123079 | 渲染主动段落 |
| `UpdateCritChance(Card, string)` | 2 | `0x75E47C0` | 123085 | 暴击文本 |
| `UpdateCardCooldownClock(string, bool, string)` | 3 | `0x75E4990` | 123094 | 冷却数字 |
| `UpdateCardCooldownFill(Card, int, int)` | 3 | `0x75E4C94` | 123103 | 冷却填充 |
| `UpdateCardAmmo(Card, string)` | 2 | `0x75E4198` | 123064 | 弹药 |
| `ResetValues()` | 0 | `0x75E7018` | 123190 | 重置（每次换卡都走） |
| `ClearCurrentCard()` | 0 | `0x75E2DC0` | 123004 | 清空 |
| `AssignTooltipFrame(ETier)` | 1 | `0x75E4884` | 123091 | 边框 |
| `Update()`（MonoBehaviour） | 0 | `0x75E0310` | 122962 | 每帧 |

真正把字符串写进 TMP 的地方是 `CardEffectTooltipController`（TypeDefIndex 3483，dump.cs:121953）：

| 成员 | 类型/签名 | 偏移 | RVA | dump.cs 行 |
|---|---|---|---|---|
| `textObject` | `TMP_Text` | **0x50** | — | 121958 |
| `SetText(string)` | 1 参数 | — | **`0x75DE518`** | 122025 |
| `SetCardEffect(bool, string)` | 2 参数 | — | `0x75DE4E0` | 122022 |
| `SetVisible(bool)` | 1 | — | `0x75DF0B0` | 122056 |
| `ClearText()` | 0 | — | `0x75DEFA8` | 122053 |

标题文本在基类：`BaseTooltipController.headerText`（`TMP_Text`，偏移 **0x30**，dump.cs:121750）。

### 4.4 推荐 hook 点（"追加文本"）

**首选：`CardTooltipTypeHandler.RenderCardUI`（TypeDefIndex 3491，dump.cs:123247，2 参数，RVA `0x75E8E5C`）**

```csharp
[HarmonyPatch(typeof(CardTooltipTypeHandler), "RenderCardUI")]
static class MyTooltipPatch {
    static void Postfix(CardTooltipController controller, CardTooltipData data) {
        // controller.headerText.text 已是本地化标题
        controller.headerText.text += "\n<size=70%>我的补充</size>";
    }
}
```

理由：BPP 就是这么做的（`bpp/.../Patches/Tooltips/BilingualItemNamePatch.cs:16-53`，postfix 里读 `controller.headerText?.text` 再追加副标题），且此方法在定位/布局前执行，追加内容会参与排版。

**次选（段落级、可插到原生块之间）：`CardTooltipController.RenderPassiveEffectTextBlock`（2 参数，RVA `0x75E42F8`）**
BPP 有 3 个补丁挂在这里（见 §4.5）。

**最底层：`CardEffectTooltipController.SetText(string)`（1 参数，RVA `0x75DE518`）**
只影响单行，且会被后续 `ClearText` 覆盖，除非只在 postfix 里做。

### 4.5 BPP `Patches/Tooltips` 清单（hook 目标 → 目的）

| 文件 | hook 目标（dump 对应） | 目的 |
|---|---|---|
| `BilingualItemNamePatch.cs:16` | `CardTooltipTypeHandler.RenderCardUI`（0x75E8E5C） | 标题后追加中文/英文副标题 |
| `BilingualItemNamePatch.cs:73` | `CardTooltipController.ResetValues`（0x75E7018） | 换卡时清掉 BPP 副标题 |
| `UpgradePreviewTooltipPatch.cs:8` | `CardController.ShowTooltips` | 安排"升级预览"tooltip |
| `UpgradePreviewValuePatch.cs:13,23` | `CardTooltipData.RenderTooltip(TooltipBuilder)`、`TooltipComponentAura.Resolve` | 预览升级后数值 |
| `UpgradePreviewCooldownPatch.cs:9` | `CooldownRenderer.RenderFromTooltip`（0x75E9FF8） | 预览升级后冷却数字 |
| `ItemEnchantPreviewPatch.cs:16` | `CardTooltipController.RenderPassiveEffectTextBlock`（0x75E42F8） | 在被动块下方插附魔预览段 |
| `ItemEnchantPreviewPatch.cs:238/248/256/264` | `ResetValues` / `ClearCurrentCard` / `OnDisable` / `OnDestroy` | 清理 BPP 段落 |
| `AggregateItemMissingTypesTooltipPatch.cs:15` | `RenderPassiveEffectTextBlock` | 追加聚合后的类型说明段 |
| `EncounterEventTooltipPatch.cs:13` | `RenderPassiveEffectTextBlock` | 追加遭遇事件说明段 |
| `HeroLevelRewardsTooltipPatch.cs:18` | `HeroLevelTooltipTypeHandler.HandleTooltip` | 英雄等级奖励明细 |
| `PackageMerchantSummaryTooltipPatch.cs:18` | `CardTooltipData.GetPassiveTooltipBlock`（0x74DFC98） | 在生成阶段就拼进商人/规则文本 |
| `QuestRewardPreviewTooltipPatch.cs:20,221` | `TooltipQuestEntry.SetData`、`TooltipQuestGroup.SetData` | 任务奖励预览 |
| `NativeTooltipSuppressionPatches.cs:32-122` | `CardTooltipController.OnDestroy`、`BaseTooltipController.OnDestroy`、`CardController.OnPointerEnter`、`ItemController.OnPointerMove`、`SkillProxyRenderer.OnPointerEnter`、`RecapItemVisualController.OnPointerEnter`、`ShowTooltipController` 等 | 手机/重放场景下抑制原生 tooltip |
| `PostCombatImpact/...RecapPatch.cs:164,178,186,194,202` | `ShowTooltipController`、`ResetValues`、`ClearCurrentCard`、`OnDisable`、`ToggleInteractabilityOnCanvas` | 战后结算 tooltip |
| `CollectionPanel/CollectionTierTooltipPatch.cs` | 收藏面板 tier tooltip | 图鉴 tier 文案 |
| `CollectionPanel/CollectionCooldownTooltipPositionPatch.cs` | 冷却 tooltip 定位 | 手机布局 |

---

## 5. 战斗 / 时间

### 5.1 `CombatSimHandler`（TypeDefIndex 1311，dump.cs:48784，`TheBazaar`，`internal sealed`）

| 成员 | 签名 | 偏移 | RVA | dump.cs 行 |
|---|---|---|---|---|
| `SpeedMultiplier` | `internal static float` | static **0x4** | get `0x72FA5EC` / set `0x72FA644` | 48793 / 48820 / 48824 |
| `CombatReadyToStart` | `public static bool` | static 0x0 | get `0x72FA534` | 48791 / 48812 |
| `Simulate` | `public Task Simulate(NetMessageCombatSim, CancellationTokenSource)` | — | **`0x72FAA34`** | 48841 |
| `HandleMessage` | `protected override Task HandleMessage(NetMessageCombatSim)` | — | `0x72FA92C` | 48834 |
| `CancelCombat` | `public void CancelCombat()` | — | `0x72FAA20` | 48837 |
| **`SetSpeed`** | **`private void SetSpeed(float, bool)`** | — | **`0x72FAB40`** | 48844 |
| `MarkCombatReadyToStart` | `public static void MarkCombatReadyToStart()` | — | `0x72FA8A0` | 48830 |
| `ShouldFastForward` | `private bool ShouldFastForward()` | — | `0x72FD82C` | 48886 |
| `ProcessEvents` | `private void ProcessEvents(ICombatSimEvent, CombatSimFrame, List<string>)` | — | `0x72FBA58` | 48865 |
| 常量 `DEFAULT_SPEED_MULTIPLIER` | `1` | — | — | 48787 |
| 常量 `FIRST_FIGHT_SPEED_MULTIPLIER` | `3` | — | — | 48788 |

**暂停**：`CombatSimHandler` 里没有 `Pause/Resume`。战斗暂停/恢复走全局：

| 入口 | 签名 | RVA | dump.cs 行 |
|---|---|---|---|
| `GameServiceManager.PauseOrUnpauseGame` | `public void PauseOrUnpauseGame(bool)` | `0x764C1C0` | 14795 |
| `GameServiceManager.GamePaused` | `public bool GamePaused { get; }`（0x120） | `0x764AE68` | 14697 |
| `GameServiceManager.EnforceMaxTimeScale` | `public void EnforceMaxTimeScale(float)` | `0x764C834` | 14807 |
| `GameServiceManager.StartOrReverseTimeLerp` | `public void StartOrReverseTimeLerp(bool)` | `0x764C5A4` | 14801 |
| `GameServiceManager.GamePauseHandler` | `public static Task GamePauseHandler(Nullable<CancellationToken>)` | `0x764C844` | 14811 |

BPP 就是这么恢复的：`ReplayBootstrap.cs:241-242` → `Singleton<GameServiceManager>.Instance.PauseOrUnpauseGame(toPauseOrUnpause: false)`。

### 5.2 `ReplayState`（TypeDefIndex 1405，dump.cs:51865，`TheBazaar`）

| 方法 | 参数个数 | RVA | dump.cs 行 | 作用 |
|---|---|---|---|---|
| `Replay()` | 0 | `0x731BEF8` | 51952 | 播放整场 |
| `Recap()` | 0 | `0x731BFA0` | 51955 | 战报 |
| `RecapBack()` | 0 | `0x731C0F4` | 51959 | 退回战报 |
| `Exit()` | 0 | `0x731C19C` | 51962 | 退出重放 |
| `OnEnter()` | 0 | `0x7319BDC` | 51906 | 进入状态（async） |
| `OnExit()` | 0 | `0x7319CB4` | 51909 | 离开状态（async） |
| `SpawnCombatCards()` | 0 | `0x731A6B0` | 51916 | 生成战斗卡 |
| `SpawnCombatCards(ECombatantId, bool)` | 2 | `0x731A788` | 51920 | |
| `DisposeCurrentCards()` | 0 | `0x731BC0C` | 51942 | |
| `DisableAllCardsVFX()` | 0 | `0x731BCE4` | 51948 | |
| `IsReplaying` | 属性，0x14 | get `0x7319A54` | 51869 / 51885 | |

`ReplayState` 里**没有**速度/暂停方法（"未找到"）——速度在 `CombatSimHandler.SetSpeed`，暂停在 `GameServiceManager`。

字段：

| 字段 | 偏移 | dump.cs 行 |
|---|---|---|
| `IsReplaying` | 0x14 | 51869 |
| `_sequence` (`CombatSequenceMessages`) | 0x18 | 51870 |
| `_combatCollection` (`ReplayCardCollection`) | 0x20 | 51871 |
| `_cardUpdates` | 0x28 | 51872 |
| `_exitRequested` | 0x38 | 51874 |

### 5.3 `UnityEngine.Time.set_timeScale` —— **在 dump 里**

`UnityEngine.Time`（TypeDefIndex 8439，dump.cs:843859）：

```text
843921: 	public static float get_timeScale() { }      // RVA 0x7FA8958
843924: 	public static void set_timeScale(float value) { } // RVA 0x7FA8980
```

| 成员 | RVA | dump.cs 行 |
|---|---|---|
| `Time.get_timeScale` | `0x7FA8958` | 843921 |
| **`Time.set_timeScale`** | **`0x7FA8980`** | 843924 |
| `Time.get_deltaTime` | `0x7FA8808` | 843897 |
| `Time.get_unscaledDeltaTime` | `0x7FA8880` | 843906 |
| `Time.get_frameCount` | `0x7FA89B8` | 843927 |

⚠️ 直接改 `Time.timeScale` 会连 UI 动画/补间一起变速。游戏自己走 `GameServiceManager`（`NormalSpeedTimeScaleValue` 0xAC、`PausedTimeScaleValue = 0`、`EnforceMaxTimeScale`）。

### 5.4 BPP `Patches/Combat` 怎么控速

```csharp
// bpp/.../Patches/Combat/CombatSpeedPatch.cs:9-19
[HarmonyPatch(typeof(CombatSimHandler), "SetSpeed")]
internal static class CombatSpeedPatch {
    [HarmonyPrefix]
    private static void Prefix(ref float speed) {
        if (!CombatStatusBar.ShouldOverrideCombatSpeed(speed)) return;
        speed = CombatStatusBar.CombatSpeedMultiplier;      // 改参数
    }
}
```

即：**prefix 改 `SetSpeed(float, bool)` 的 `speed` 入参**（`ref float speed`），不动 `Time.timeScale`。
同一目录下还有：

| 文件 | hook | 作用 |
|---|---|---|
| `CombatSpeedPatch.cs:9` | `CombatSimHandler.SetSpeed` | 战斗倍速 |
| `CombatSimulationPatches.cs:13` | `CombatSimHandler.Simulate` | 捕获战斗消息 / 延迟模拟 |
| `CombatSimulationPatches.cs:42` | `FinalBlowSlowDownController.Process`（RVA `0x72CE1DC`，dump.cs:43238） | 每战斗帧发事件 |
| `CombatReplayStateExitPatch.cs:8` | `ReplayState.Exit` | 重放退出清理 |
| `CombatReplayItemPresentationReadinessPatch.cs:14,20` | `ItemController.Setup(Card)`、`ReplayState.SpawnCombatCards()` | 重放卡展示 |
| `CombatReplayFinalizeRunInitializationPatch.cs:7` | `StartRunAppState.FinalizeRunInitialization` | 开局注入 |

---

## 6. UI 创建

### 6.1 游戏用的是 UGUI `Text` 还是 TMP？

**两者都在，主 UI 是 TMP。**

| 统计项（dump 全文匹配行数） | 数量 |
|---|---|
| `TextMeshProUGUI` | 333 |
| `TMP_Text` | 696 |
| UGUI `UnityEngine.UI.Text` 类型字段（`private/public Text xxx; // 0x..`） | 85 |

证据：`CardController.debugFrame` 是 `TextMeshPro`（dump.cs:3752），tooltip 用 `TMP_Text`（dump.cs:121750 / 121958），同时 `Text` 类完整存在于 `UnityEngine.UI`（dump.cs:1411706）。

**建议**：新面板用 `TextMeshProUGUI`，字体从现成 TMP 组件上借（§6.6）；`UnityEngine.UI.Text` 更适合"只想快速出字"且能接受默认 Arial 的场景。

### 6.2 创建对象

| 目标 | 方法 | 参数个数 | RVA | dump.cs 行 |
|---|---|---|---|---|
| `new GameObject(string name)` | `GameObject..ctor(string)` | 1 | **`0x7F9B48C`** | 840751 |
| `new GameObject()` | `GameObject..ctor()` | 0 | `0x7F9B66C` | 840754 |
| `new GameObject(string, Type[])` | `.ctor(string, Type[])` | 2 | `0x7F9B6D0` | 840757 |
| `go.AddComponent(Type)` | `GameObject.AddComponent(Type)` | 1 | **`0x7F99E68`** | 840602 |
| `go.AddComponent<T>()` | `GameObject.AddComponent<T>()` | 0（泛型） | **`0x7F99E68`** | 840605 |
| `go.GetComponent(Type)` | `GameObject.GetComponent(Type)` | 1 | `0x7F95D34` | 840349 |
| `go.SetActive(bool)` | `GameObject.SetActive` | 1 | `0x7F9A624` | 840657 |
| `Object.Destroy(Object)` | `UnityEngine.Object.Destroy` | 1 | `0x7FA215C` | 842330 |
| `Object.DontDestroyOnLoad` | `UnityEngine.Object.DontDestroyOnLoad` | 1 | `0x7FA2498` | 842360 |
| `Object.Instantiate(Object)` | `UnityEngine.Object.Instantiate` | 1 | `0x7FA1A78` | 842254 |

### 6.3 组件 `.ctor` RVA

| 类型 | 全名 | `.ctor` RVA | dump.cs 行 |
|---|---|---|---|
| `Canvas` | `UnityEngine.Canvas` | `0x82CA7EC` | 1652749 |
| `CanvasScaler` | `UnityEngine.UI.CanvasScaler` | `0x82D5938`（protected） | 1408975 |
| `GraphicRaycaster` | `UnityEngine.UI.GraphicRaycaster` | `0x80C5048`（protected） | 1407383 |
| `Image` | `UnityEngine.UI.Image` | `0x80C71E8`（protected） | 1407713 |
| `Text` | `UnityEngine.UI.Text` | `0x82EAF78`（protected） | 1411749 |
| `TextMeshProUGUI` | `TMPro.TextMeshProUGUI` | `0x7D25DA8` | 1322223 |
| `RectTransform` | `UnityEngine.RectTransform` | `0x7FAC1DC` | 844423 |

> `.ctor` 是 IL 构造函数，实际 Mod 里**不要手调**，统一用 `AddComponent<T>()`（RVA 见上）。列在这里是为了确认类型存在与签名。

### 6.4 文本属性（setter RVA）

**TMP（`TMPro.TMP_Text`，TypeDefIndex 16185，dump.cs:1318272）**

| 属性 | getter RVA | setter RVA | dump.cs 行 |
|---|---|---|---|
| `text` | `0x7CCB2A0` | **`0x7CCB384`** | 1318686 / 1318689 |
| `font` (`TMP_FontAsset`) | `0x7CCB490` | `0x7CCB498` | 1318704 / 1318707 |
| `color` | `0x7CCB818` | `0x7CCB82C` | 1318734 / 1318737 |
| `fontSize` (float) | `0x7CCC008` | **`0x7CCC010`** | 1318812 / 1318815 |
| `fontStyle` | `0x7CCC320` | `0x7CCC328` | 1318845 / 1318848 |
| `alignment` (`TextAlignmentOptions`) | `0x7CCC3EC` | `0x7CCC3FC` | 1318866 / 1318869 |
| `enableWordWrapping` | `0x7CCC6E4` | `0x7CCC6F8` | 1318920 / 1318923 |
| `richText` | `0x7CCCD8C` | `0x7CCCD94` | 1318971 / 1318974 |

**UGUI（`UnityEngine.UI.Text`，TypeDefIndex 17837，dump.cs:1411706）**

| 属性 | getter RVA | setter RVA | dump.cs 行 |
|---|---|---|---|
| `text` | `0x82EB514` | **`0x82EB51C`** | 1411770 / 1411773 |
| `font` (`Font`) | `0x82EB2CC` | `0x82EB3F8` | 1411764 / 1411767 |
| `fontSize` (int) | `0x82EB88C` | `0x82EB8A4` | 1411812 / 1411815 |
| `alignment` (`TextAnchor`) | `0x82EB7C8` | `0x82EB7E0` | 1411800 / 1411803 |
| `supportRichText` | `0x82EB600` | `0x82EB618` | 1411776 / 1411779 |

### 6.5 `Graphic` / `Image` / `RectTransform` / `Canvas`

`Graphic`（TypeDefIndex 17751，dump.cs:1407053）——`Image`/`Text`/`TMP_Text` 的共同基类：

| 属性 | getter | setter | dump.cs 行 |
|---|---|---|---|
| `color` | `0x80C16EC` | `0x80C16F8` | 1407108 / 1407111 |
| `raycastTarget` | `0x80C1730` | `0x80C1738` | 1407114 / 1407117 |

`Image`（TypeDefIndex 17766，dump.cs:1407562）：

| 属性 | getter | setter | dump.cs 行 |
|---|---|---|---|
| `sprite` | `0x80C699C` | `0x80B97C0` | 1407635 / 1407638 |
| `type` | `0x80C6CB8` | `0x80B9B30` | 1407653 / 1407656 |
| `preserveAspect` | `0x80C6CC0` | `0x80C6CC8` | 1407659 / 1407662 |
| `fillCenter` | `0x80C6D48` | `0x80C6D50` | 1407665 / 1407668 |

`RectTransform`（TypeDefIndex 8453，dump.cs:844296）——**手机布局最关键**：

| 属性 | getter RVA | setter RVA | dump.cs 行 |
|---|---|---|---|
| `anchorMin` | `0x7FAA850` | **`0x7FAA914`** | 844331 / 844334 |
| `anchorMax` | `0x7FAA9D4` | **`0x7FAAA98`** | 844337 / 844340 |
| `anchoredPosition` | `0x7FAAB58` | **`0x7FAAC1C`** | 844343 / 844346 |
| `sizeDelta` | `0x7FAACDC` | **`0x7FAADA0`** | 844349 / 844352 |
| `pivot` | `0x7FAAE60` | **`0x7FAAF24`** | 844355 / 844358 |
| `offsetMin` | `0x7FAB160` | `0x7FAB1B4` | 844367 / 844370 |
| `offsetMax` | `0x7FAB2A8` | `0x7FAB348` | 844373 / 844376 |
| `SetParent(Transform)` | — | `0x7FAD374` | 844659 |
| `SetParent(Transform, bool)` | — | `0x7FAD3B8` | 844663 |

`Canvas`（TypeDefIndex 23645，dump.cs:1652488）：

| 属性 | setter RVA | dump.cs 行 |
|---|---|---|
| `renderMode` | `0x82C822C` | 1652561 |
| `scaleFactor` | `0x82C8530` | 1652573 |
| `sortingOrder` | `0x82C905C` | 1652618 |
| `worldCamera` | `0x82C9F74` | 1652695 |
| `pixelPerfect` | `0x82C8B30` | 1652597 |

`CanvasScaler`（TypeDefIndex 17784，dump.cs:1408859）：

| 属性 | setter RVA | dump.cs 行 |
|---|---|---|
| `uiScaleMode` | `0x82D5820` | 1408918 |
| `referenceResolution` | `0x82D5860` | 1408936 |
| `screenMatchMode` | `0x82D58D4` | 1408942 |
| `matchWidthOrHeight` | `0x82D58E4` | 1408948 |
| `scaleFactor` | `0x82D5840` | 1408930 |

`GraphicRaycaster`（TypeDefIndex 17754，dump.cs:1407331）：`ignoreReversedGraphics` setter `0x80C5020`（1407368）、`blockingObjects` setter `0x80C5030`（1407374）。

### 6.6 找现成字体 / 画布

| 目标 | 方法 | RVA | dump.cs 行 |
|---|---|---|---|
| 找任意 T | `Object.FindObjectOfType<T>()` | **`0x4789D94`** | 842422 |
| 找任意（Type） | `Object.FindObjectOfType(Type)` | `0x7FA28D0` | 842480 |
| 找全部 T | `Object.FindObjectsOfType<T>()` | `0x7FA288C` | 842388 |
| 内置资源 | `Resources.GetBuiltinResource(Type, string)` | `0x7F907F0` | 834733 |
| 由 `Font` 造 TMP 字体 | `TMP_FontAsset.CreateFontAsset(Font)` | `0x7CB1248` | 1313964 |
| 由系统字体名造 TMP 字体 | `TMP_FontAsset.CreateFontAsset(string family, string style, int size)` | `0x7CB0880` | 1313955 |

**借字体（推荐）**：

```csharp
var anyTmp = UnityEngine.Object.FindObjectOfType<TMPro.TextMeshProUGUI>(); // 0x4789D94
var fontAsset = anyTmp != null ? anyTmp.font : TMP_FontAsset.CreateFontAsset("Arial", "Regular", 90);
myTmp.font = fontAsset;                 // 0x7CCB498
```

**借画布**：`FindObjectOfType<Canvas>()`（`Canvas` 是 `UnityEngine.Canvas`，dump.cs:1652488），把自己的面板 `SetParent(canvas.transform, false)`（`0x7FAD3B8`）并设 `sortingOrder` 更高。

### 6.7 `UnityEngine.Input` 触摸

`Input`（TypeDefIndex 24040，dump.cs:1666169）：

| 成员 | 签名 | RVA | dump.cs 行 |
|---|---|---|---|
| `get_touchCount` | `public static int touchCount { get; }` | **`0x8023D34`** | 1666298 |
| `GetTouch` | `public static Touch GetTouch(int index)` | **`0x8023604`** | 1666235 |
| `get_touchSupported` | `public static bool touchSupported` | `0x8023CC8` | 1666294 |
| `get_mousePosition` | `public static Vector3 mousePosition` | `0x802385C` | 1666258 |
| `GetMouseButtonDown` | `public static bool GetMouseButtonDown(int)` | `0x802358C` | 1666227 |
| `GetKeyDown(KeyCode)` | `public static bool GetKeyDown(KeyCode)` | `0x80237F8` | 1666252 |

⚠️ 本作还引用了新版 Input System：`InputSystemUIInputModule`（TypeDefIndex 10317，dump.cs:938650，`Process()` RVA `0x78ABA3C`，dump.cs:939056）。`Input.touchCount` 属于旧 Input Manager，只要项目 `Active Input Handling` 含 "Both/Old" 就可用；若只开 New，则改用 `UnityEngine.InputSystem.Touchscreen.current`。运行时可用 `EventSystem.current.currentInputModule` 的类型来判定实际模块。

---

## 7. 每帧回调

**两个都存在于 dump 里**（不是"未找到"）：

| 方法 | 签名 | RVA | dump.cs 行 |
|---|---|---|---|
| `UnityEngine.EventSystems.EventSystem.Update` | `protected virtual void Update()` | **`0x82F9EA8`** | 1414641 |
| `UnityEngine.EventSystems.StandaloneInputModule.Process` | `public override void Process()` | **`0x82FFFC4`** | 1415521 |
| `StandaloneInputModule.ProcessTouchEvents` | `private bool ProcessTouchEvents()` | `0x83001B0` | 1415524 |
| `EventSystem.current` | `public static EventSystem current { get; }` | `0x82F72B8` | 1414550 |
| `EventSystem.IsPointerOverGameObject()` | `public bool` | `0x82F9620` | 1414610 |

但**手机端很可能不是 `StandaloneInputModule`**：本作引用了新 Input System 的 `InputSystemUIInputModule`（dump.cs:938650），其 `Process()` 才是每帧入口：

| 方法 | RVA | dump.cs 行 |
|---|---|---|
| `InputSystemUIInputModule.Process()` | `0x78ABA3C` | 939056 |
| `InputSystemUIInputModule.ProcessPointer(...)` | 见块内 | 938809 |

**"每帧必被调用"的可靠候选（按稳定性排序）**：

1. `TheBazaar.Utilities.UpdateLoop.Update()` —— RVA **`0x73F7D54`**，dump.cs:73502。游戏所有 `ITickable` 都从这里派发（`RegisterTickable` RVA `0x73F7944`，dump.cs:73487；接口 `IUpdateLoop` dump.cs:72414，`ITickable` dump.cs:72405）。挂它等于"在游戏的主循环里"。
2. `TheBazaar.Utilities.UpdateLoop.LateUpdate()` —— RVA `0x73F7E78`，dump.cs:73505。
3. `CardTooltipController.Update()` —— RVA `0x75E0310`，dump.cs:122962（只在有 tooltip 组件时跑，不够通用）。
4. `FinalBlowSlowDownController.Process(int framesLeft)` —— RVA `0x72CE1DC`，dump.cs:43238（**每个战斗帧**，BPP 用它发 `CombatFrameAdvanced`）。
5. `EventSystem.Update` / `InputSystemUIInputModule.Process` —— 只在有 EventSystem 时跑，且会被输入模块类型影响。

**推荐**：Mod 自己的 `MonoBehaviour.Update()`（BepInEx 插件挂在一个 `DontDestroyOnLoad` 的 GameObject 上）即可；需要"精确跟随游戏 tick"时挂 `UpdateLoop.Update`，需要"战斗帧"时挂 `FinalBlowSlowDownController.Process`。

---

## 8. 建议的 mod 调用链（最小路径）

```csharp
// ─────────────────────────────────────────────────────────────
// 0) 初始化：BepInEx 插件 Awake，建一个常驻宿主对象
// ─────────────────────────────────────────────────────────────
public class MobileMod : BaseUnityPlugin {
    void Awake() {
        var host = new GameObject("MobileBppHost");           // RVA 0x7F9B48C
        UnityEngine.Object.DontDestroyOnLoad(host);           // RVA 0x7FA2498
        host.AddComponent<MobileModBehaviour>();              // RVA 0x7F99E68
    }
}

// ─────────────────────────────────────────────────────────────
// 1) 取数据：静态库（图鉴）+ 当前对局
// ─────────────────────────────────────────────────────────────
JsonGameDataManager Static() {
    if (!TheBazaar.Data.IsManagerCreated()) return null;       // 0x72EFD44
    var o = TheBazaar.Data.GetStatic();                       // 0x72EFCEC
    return o as JsonGameDataManager
        ?? (o as Task<JsonGameDataManager>)?.Result;          // 版本兼容
}

void DumpCollection() {
    var mgr = Static(); if (mgr == null) return;
    foreach (var kv in mgr.GetCardMap()) {                    // 0x74AE390（重，放后台线程）
        if (kv.Value is not TCardItem card) continue;
        string name = card.Localization?.Title?.Text;         // 0x78 -> 0x10 -> 0x18
        string desc = card.Localization?.Description?.Text;   // 0x78 -> 0x18 -> 0x18
        ETier  tier = card.StartingTier;                      // 0x38
        ECardSize size = card.Size;                           // 0x3C
        var heroes = card.Heroes;                             // 0x40
        var tags = card.Tags;                                 // 0x48
        if (card.Tiers.TryGetValue(tier, out var t)) {        // 0x90
            int price    = t.Attributes[(ECardAttributeType)37]; // BuyPrice
            int cooldown = t.Attributes[(ECardAttributeType)4];  // Cooldown
        }
    }
}

void DumpRun() {
    var run = TheBazaar.Data.Run;                             // 0x10
    if (run == null) return;                                  // 不在局内
    var me = run.Player;                                      // 0x40
    var hand = me?.Hand?.GetItems();                          // Player.Hand 0x28 -> IPlayerInventory.GetItems
    foreach (var c in hand) {
        if (c is not ItemCard item) continue;
        Guid tplId   = item.TemplateId;                        // 0x18
        ETier tier   = item.Tier;                              // 0x50
        var ench     = item.Enchantment;                       // 0x80
        var socket   = item.LeftSocketId;                      // 0x60
        var section  = item.Section;                           // 0x68
        var tpl      = item.Template as TCardItem             // 0x78
                    ?? Static()?.GetCardById(tplId) as TCardItem; // 0x74B8F08
    }
    // 棋盘 UI 上的卡（拿到 CardController → Card）
    var bm = Singleton<BoardManager>.Instance;                // static field 0x0
    foreach (var s in bm.playerItemSockets)                   // 0x60
        if (s != null) { var card = s.GetComponentInChildren<CardController>()?.CardData; }
}

// ─────────────────────────────────────────────────────────────
// 2) 画面板：UGUI + TMP，挂在现有 Canvas 下
// ─────────────────────────────────────────────────────────────
void BuildPanel() {
    var rootCanvas = UnityEngine.Object.FindObjectOfType<Canvas>();  // 0x4789D94
    var go = new GameObject("MobileBppPanel");                        // 0x7F9B48C
    var rt = go.AddComponent<RectTransform>();                        // 0x7F99E68
    rt.SetParent(rootCanvas.transform, false);                        // 0x7FAD3B8
    rt.anchorMin = Vector2.zero;  rt.anchorMax = Vector2.one;         // 0x7FAA914 / 0x7FAAA98
    rt.offsetMin = Vector2.zero;  rt.offsetMax = Vector2.zero;        // 0x7FAB1B4 / 0x7FAB348
    rt.pivot = new Vector2(0.5f, 0.5f);                               // 0x7FAAF24

    var bg = go.AddComponent<Image>();                                // Image, ctor 0x80C71E8
    bg.color = new Color(0, 0, 0, 0.8f);                              // Graphic.set_color 0x80C16F8

    var labelGo = new GameObject("Title");
    var label = labelGo.AddComponent<TextMeshProUGUI>();              // ctor 0x7D25DA8
    label.transform.SetParent(go.transform, false);                   // Transform.SetParent 0x7FAD3B8
    label.text = "图鉴";                                              // TMP set_text 0x7CCB384
    label.fontSize = 36;                                              // 0x7CCC010
    label.alignment = TextAlignmentOptions.Center;                    // 0x7CCC3FC
    label.font = UnityEngine.Object.FindObjectOfType<TextMeshProUGUI>()?.font
                 ?? TMP_FontAsset.CreateFontAsset("Arial", "Regular", 90); // 0x7CB0880

    // 需要点时把 GraphicRaycaster 补齐（面板通常挂在已有 Canvas 下，可省）
    if (go.GetComponent<GraphicRaycaster>() == null) go.AddComponent<GraphicRaycaster>(); // ctor 0x80C5048
}

// ─────────────────────────────────────────────────────────────
// 3) 处理触摸：自己的 Update 里读 Input
// ─────────────────────────────────────────────────────────────
void Update() {
    if (Input.touchCount == 0) return;                    // 0x8023D34
    var t = Input.GetTouch(0);                            // 0x8023604
    if (t.phase != TouchPhase.Began) return;
    bool overUI = EventSystem.current != null &&
                  EventSystem.current.IsPointerOverGameObject(t.fingerId); // 0x82F9628
    if (overUI) return;                                   // 让原生 UI 先吃
    // 命中自己的矩形：用 RectTransformUtility.RectangleContainsScreenPoint(...)
    HandleTap(t.position);
}

// ─────────────────────────────────────────────────────────────
// 4) 追加 tooltip 文本（Harmony postfix）
// ─────────────────────────────────────────────────────────────
[HarmonyPatch(typeof(CardTooltipTypeHandler), "RenderCardUI")]   // 0x75E8E5C
static class TooltipAppendPatch {
    static void Postfix(CardTooltipController controller, CardTooltipData data) {
        var extra = MyDb.GetExtraText(data.CardTemplate);         // 自己的数据
        if (string.IsNullOrEmpty(extra)) return;
        controller.headerText.text += "\n" + extra;               // BaseTooltipController.headerText 0x30
    }
}

// ─────────────────────────────────────────────────────────────
// 5) 战斗控速 / 暂停（可选）
// ─────────────────────────────────────────────────────────────
[HarmonyPatch(typeof(CombatSimHandler), "SetSpeed")]              // 0x72FAB40
static class SpeedPatch { static void Prefix(ref float speed) => speed = MySpeed; }

void PauseGame(bool paused) =>
    Singleton<GameServiceManager>.Instance.PauseOrUnpauseGame(paused);  // 0x764C1C0
```

---

## 9. 未找到 / 限制（明确记录，避免后续误判）

| 要找的东西 | 结论 | 依据 |
|---|---|---|
| `JsonGameDataManager.Instance` / 单例成员 | **未找到**（不存在） | dump.cs:93855-93995 全块无 `Instance` |
| `JsonGameDataManager` 的调用者 | **dump 无调用图**，只有 `Data.CreateManager/CreateFallbackManager` 两个入口 | dump.cs:47150 / 47153 |
| `BoardState` 类 | **未找到**。只有 `BoardStateSO`(442)、`BoardStatePresentation`(1470)、`BoardStatePresentationEventSO`(441)、`BoardManager`(348) | dump.cs:17172 / 53714 / 17163 / 13245 |
| `ShopState` / `ShopInventory` / `ShopController` | **未找到**（`ShopState` 全文 0 命中） | — |
| `StashState` | **未找到**；仓库是 `Player.Stash`（`IPlayerInventory`） | dump.cs:1607752 |
| `CombatSimHandler.Pause/Resume` | **未找到**；暂停在 `GameServiceManager` | dump.cs:14795 |
| `ReplayState` 的速度/暂停方法 | **未找到**；速度在 `CombatSimHandler.SetSpeed` | dump.cs:48844 |
| `Time.timeScale` | **找到了**，RVA `0x7FA8980` | dump.cs:843924 |
| `EventSystem.Update` / `StandaloneInputModule.Process` | **都找到了**（RVA `0x82F9EA8` / `0x82FFFC4`） | dump.cs:1414641 / 1415521 |

其它限制：

- 泛型方法的 RVA 在 dump 里给的是"每个实例化一份"的列表（`/* GenericInstMethod */`），例如 `Data.GetCards<object>` 是 `0x45D1810`；直接按泛型反射调用时需要确保实例化已存在。
- `internal` / `private` 方法（如 `CombatSimHandler.SetSpeed`、`CardTooltipTypeHandler.RenderCardUI`）Harmony 可以 patch，但**不能**直接 C# 调用；要调用就反射或用 `AccessTools`。
- `GetCardMap()` 首次调用有秒级卡顿，务必后台线程 + 缓存（BPP 的做法：`BppStaticDataAccess.cs:53-88`）。
- `Singleton<T>.Instance` 是**泛型静态字段**，在 Il2CppInterop 下要通过 `Singleton<GameServiceManager>.Instance` 访问（BPP 同写法）。

---

## 10. 行号索引（全部关键类型）

| 类型 | 命名空间 | TypeDefIndex | dump.cs 行 |
|---|---|---|---|
| `JsonGameDataManager` | `TheBazaar.DataManagement.Json` | 2704 | 93855 |
| `Data` | `TheBazaar` | 1278 | 46698 |
| `AppState` | `TheBazaar` | 1374 | 50806 |
| `RunAppState` | `TheBazaar` | 1408 | 52017 |
| `EncounterState` | `TheBazaar` | 1382 | 51279 |
| `CombatState` | `TheBazaar` | 1380 | 51242 |
| `ChoiceState` | `TheBazaar` | 1377 | 51183 |
| `PedestalState` | `TheBazaar` | 1395 | 51586 |
| `ReplayState` | `TheBazaar` | 1405 | 51865 |
| `GameServiceManager` | （全局） | 360 | 14613 |
| `BoardManager` | （全局） | 348 | 13245 |
| `Singleton<T>` | （全局） | 472 | 17951 |
| `Run` | `BazaarGameClient.Domain.Models` | 22404 | 1607858 |
| `RunState` | `BazaarGameClient.Domain.Models` | 22405 | 1608041 |
| `Player` | `BazaarGameClient.Domain.Models` | 22403 | 1607728 |
| `Card` | `BazaarGameClient.Domain.Models.Cards` | 22413 | 1608474 |
| `ItemCard` | `BazaarGameClient.Domain.Models.Cards` | 22424 | 1608868 |
| `IPlayerInventory` | `BazaarGameShared.Domain.Players` | 9711 | 896395 |
| `SocketedContainer` | `BazaarGameShared.Domain.Cards` | 9923 | 913468 |
| `InstanceId` | `BazaarGameShared.Domain.Core` | 9980 | 918086 |
| `ITCard` | `BazaarGameShared.Domain.Cards` | 9921 | 913372 |
| `TCardBase` | `BazaarGameShared.Domain.Cards` | 9927 | 913746 |
| `TCardItem` | `BazaarGameShared.Domain.Cards.Item` | 9948 | 915877 |
| `TCardLocalization` | `BazaarGameShared.Domain.Cards` | 9929 | 914140 |
| `TCardTier` | `BazaarGameShared.Domain.Cards` | 9930 | 914257 |
| `TLocalizableText` | `BazaarGameShared.Domain.Core` | 9981 | 918155 |
| `TMonster` | `BazaarGameShared.Domain.Players` | 9713 | 896555 |
| `TCollectible` | `BazaarGameShared.Domain.Collectibles` | 9915 | 912729 |
| `CardTooltipData` | `TheBazaar.Tooltips` | 2813 | 97487 |
| `BaseTooltipController` | `TheBazaar.UI.Tooltips` | 3480 | 121739 |
| `CardTooltipController` | `TheBazaar.UI.Tooltips` | 3490 | 122199 |
| `CardEffectTooltipController` | `TheBazaar.UI.Tooltips` | 3483 | 121953 |
| `CardTooltipTypeHandler` | `TheBazaar.UI.Tooltips` | 3491 | 123230 |
| `CooldownRenderer` | `TheBazaar.UI.Tooltips` | 3492 | 123263 |
| `CombatSimHandler` | `TheBazaar` | 1311 | 48784 |
| `FinalBlowSlowDownController` | `TheBazaar` | 1197 | 43219 |
| `UpdateLoop` | `TheBazaar.Utilities` | 2074 | 73464 |
| `IUpdateLoop` / `ITickable` | `TheBazaar.Utilities` | 2050 / 2049 | 72414 / 72405 |
| `UnityEngine.GameObject` | `UnityEngine` | 8376 | 840312 |
| `UnityEngine.Object` | `UnityEngine` | 8410 | 842060 |
| `UnityEngine.RectTransform` | `UnityEngine` | 8453 | 844296 |
| `UnityEngine.Canvas` | `UnityEngine` | 23645 | 1652488 |
| `UnityEngine.UI.Graphic` | `UnityEngine.UI` | 17751 | 1407053 |
| `UnityEngine.UI.Image` | `UnityEngine.UI` | 17766 | 1407562 |
| `UnityEngine.UI.Text` | `UnityEngine.UI` | 17837 | 1411706 |
| `UnityEngine.UI.CanvasScaler` | `UnityEngine.UI` | 17784 | 1408859 |
| `UnityEngine.UI.GraphicRaycaster` | `UnityEngine.UI` | 17754 | 1407331 |
| `TMPro.TMP_Text` | `TMPro` | 16185 | 1318272 |
| `TMPro.TextMeshProUGUI` | `TMPro` | 16225 | 1321942 |
| `TMPro.TMP_FontAsset` | `TMPro` | 16092 | 1313686 |
| `UnityEngine.Font` | `UnityEngine` | 24246 | 1671153 |
| `UnityEngine.Time` | `UnityEngine` | 8439 | 843859 |
| `UnityEngine.Input` | `UnityEngine` | 24040 | 1666169 |
| `EventSystem` | `UnityEngine.EventSystems` | 17902 | 1414509 |
| `StandaloneInputModule` | `UnityEngine.EventSystems` | 17917 | 1415401 |
| `InputSystemUIInputModule` | `UnityEngine.InputSystem.UI` | 10317 | 938650 |

---

*本文只做调查记录；`work/` 与 `bpp/` 未做任何修改。*
