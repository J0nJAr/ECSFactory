## 들어가며

지금까지 ECS의 뼈대(Registry, World, View)와 결정론 수학(Fixed64)을 만들었다. 이제 서버와 클라이언트가 "같은 세계"를 공유하려면 서로 대화를 해야 한다.

그런데 컴퓨터끼리의 대화는 사람처럼 *"Transform 컴포넌트 좀 줄래?"* 라고 말하는 게 아니다. 결국 선 위로 흐르는 건 **바이트 덩어리** 하나다. 그 바이트 덩어리를 어떻게 규칙 있게 포장하고(패킷), 누가 그 포장을 책임지는지(`NetworkManager`)를 정해야 한다.

한 가지 짚고 갈 게 있다. 이 게임은 **싱글플레이에서도 스스로 서버를 연다.** 화면을 그리고 입력을 받는 *프론트(클라이언트)* 와, 게임 로직을 굴리고 상태를 검증하는 *백엔드(서버)* 를 처음부터 분리해 두고, 싱글플레이라도 이 둘을 같은 프로세스 안에 함께 띄워 서로 통신시킨다. 이렇게 하면 *'싱글플레이 전용 코드'* 를 따로 짤 필요가 없다. 싱글이든, 호스트든, 원격 멀티플레이든 클라와 서버는 **언제나 똑같이 패킷으로만** 대화하기 때문이다. (같은 프로세스 안에서 어떻게 "통신"을 흉내 내는지는 뒤에서 `MemoryConnection`으로 다룬다.)

다만 이건 **설계 전제**라는 점은 밝혀 두자. 05 시점의 코드가 이미 그렇다는 말은 아니다. 05 시점(`bf71ab2`)의 `GameClient/main.cpp`·`GameServer/main.cpp`는 아직 빈 골격이라 `UDPConnection`도 `NetworkManager`도 부르지 않는다. 이 글은 그 배치가 딛고 설 **통로와 패킷 계층**을 먼저 만드는 편이고, 실제 앱 배선은 다음 커밋 `0cf2063`에서 붙는다.

> ⚠️ **이 글이 다루는 시점을 먼저 못 박아 두자.** 이 글은 커밋 `bf71ab2`("feat : 네트워크 계층 구현")의 기록이다. 그런데 이 커밋은 **그 자체로는 빌드되지 않는다.** `NetworkManager`가 `WorldSnapshot`·`ComponentSnapshot`·`ComponentTypeDescriptor`를 쓰는데, 그 시점 `ECS/World.hpp`에는 이 타입들이 **하나도 없다**(전방선언조차 없다). 정의는 바로 다음 커밋 `0cf2063`("월드 스냅샷/엔진/앱 네트워크 연동 기반")에서 들어온다. 그러니까 05는 *"스냅샷을 실어 나를 관(管)을 먼저 판"* 커밋이고, 실을 물건은 그다음에 만들어졌다. 아래에서 `SendWorldSnapshot`/`EncodeWorldSnapshot`이 나올 때는 **"다음 커밋에서 채워질 자리를 미리 파둔 것"** 으로 읽으면 된다. 커밋 하나를 곧 완성된 기능 하나로 읽으면 안 된다는 좋은 예다.

> 2026-07-09 업데이트: 이 글의 `pendingInputs`/sequence 기반 입력 큐 설명은 05 단계의 기록이다. 이 자료 구조는 이후 결정론 입력 lockstep을 만들면서 통째로 갈아엎혔다. 지금 서버는 입력을 도착 순서 큐 대신 tick별 슬롯(`inputByTick`)에 넣고, `ConfirmInputs(tick)`으로 그 tick의 입력을 확정한 뒤 `InputFrame`을 reliable로 전 클라에 재전파한다. 그 최종 형태는 8편에서 다룬다.

---

## 우리가 진짜 보내는 것: 바이트

가장 먼저 떠오르는 생각은 이렇다. *"구조체를 통째로 보내면 되잖아?"*

```cpp
InputCommand cmd = ...;
connection.Send(&cmd, sizeof(cmd)); // 이러면 되는 거 아냐?
```

안타깝게도 이건 거의 항상 터진다. 이유는 세 가지다.

1. **구조체 패딩.** 컴파일러는 정렬(alignment)을 위해 멤버 사이에 보이지 않는 빈 바이트를 끼워 넣는다. 같은 구조체라도 컴파일러나 플래그가 다르면 크기와 배치가 달라질 수 있다.
2. **엔디안(byte order).** 어떤 CPU는 숫자를 작은 바이트부터, 어떤 CPU는 큰 바이트부터 저장한다. 메모리를 그대로 보내면 받는 쪽에서 숫자가 뒤집힌다.
3. **trivial하지 않은 타입.** 안에 포인터나 `std::vector`가 있으면 전송되는 건 "그 시점의 메모리 주소"다. 받는 쪽에선 의미 없는 쓰레기값이 된다.

> trivial(trivially copyable, 자명하게 복사 가능) : 메모리 바이트를 그대로 통째로 복사(memcpy)해도 멀쩡한 타입

즉, 컴퓨터마다 다른 계산 방식/저장 방식과 단순하게 공유되지 않는 데이터들이 존재하기 때문이다.
그래서 우리는 보낼 값을 **한 바이트씩 명시적으로** 직렬화(serialize)하기로 한다. protobuf 같은 외부 라이브러리에 기대지 않고 직접 짜는 이유는, 이 프로젝트의 목표가 *"의존성 최소화 + 내가 모든 바이트를 이해하기"* 이기 때문이다.

---

## 통신 통로 추상화: `INetworkConnection`

여기서 결정을 하나 먼저 해야 한다. 패킷을 만드는 코드가 *"이게 같은 프로세스 안의 통신인지, 진짜 UDP 소켓인지"* 를 알아야 할까?

**몰라야 한다.** 그래야 싱글플레이/호스트(같은 프로세스)와 원격 멀티플레이(UDP)가 **똑같은 패킷 코드**를 쓸 수 있다. 그래서 통로를 인터페이스로 추상화한다.

여기서 한 가지 설계 선택이 중요하다. 통로를 **"바이트 파이프"** 대신 **"메시지 파이프"** 로 둔다. 주고받는 단위는 "바이트 N개"가 아닌 **메시지 한 통**이고, *"한 메시지가 어디서 끝나는가(경계)"* 는 **각 통로가 알아서** 책임진다.

```cpp
class INetworkConnection {
public:
    virtual ~INetworkConnection() = default;

    // 메시지 하나를 통째로 보낸다.
    virtual bool Send(std::span<const uint8_t> message) = 0;
    // 메시지 하나를 통째로 받는다. 받을 게 없으면 nullopt.
    virtual std::optional<std::vector<uint8_t>> Receive() = 0;
};
```

왜 "메시지 단위"가 중요한지는, 통로마다 경계를 다루는 방식이 전혀 다르기 때문이다.

### MemoryConnection: 같은 프로세스 안의 가짜 네트워크

싱글플레이라고 서버 로직을 따로 안 짜 두면, 나중에 멀티플레이를 붙일 때 코드가 두 벌이 된다. 그래서 싱글/호스트에서도 "서버 월드"를 그대로 돌리고, 클라와 서버를 **메모리 큐**로 연결한다.

```cpp
auto [clientPipe, serverPipe] = MemoryConnection::CreatePair();
```

`CreatePair`는 서로를 가리키는 두 끝을 만든다. 한쪽에서 `Send`하면 메시지가 **통째로** 상대 큐에 들어가고(`Enqueue`), 상대가 `Receive`하면 그 메시지를 통째로 꺼낸다. 메시지를 `std::vector` 단위로 큐에 넣으니 **경계는 vector 그 자체가 들고 있다**(길이 접두 같은 게 전혀 필요 없다).

```cpp
bool MemoryConnection::Send(std::span<const uint8_t> message) {
    auto peer = m_peer.lock();
    if (!peer) return false;
    peer->Enqueue(std::vector<uint8_t>(message.begin(), message.end())); // 통째로
    return true;
}

std::optional<std::vector<uint8_t>> MemoryConnection::Receive() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_inboundMessages.empty()) return std::nullopt; // 없으면 즉시 nullopt
    auto message = std::move(m_inboundMessages.front());
    m_inboundMessages.pop();
    return message;
}
```

### UDPConnection: 진짜 원격

원격 클라이언트는 winsock UDP 소켓으로 연결한다. 쓰는 방식은 이렇게 의도했다. 서버는 `Bind(포트)`로 포트를 열고, 클라는 `Connect(주소, 포트)`로 보낼 대상을 정한다. (실제로 포트 번호 `6780`을 박아 넣고 이 두 함수를 부르는 앱 코드는 05 커밋에 없다. `UDPConnection` 클래스만 만들어 둔 상태이고, 배선은 다음 커밋 `0cf2063`의 `GameServer/main.cpp`·`GameClient/main.cpp`에서 들어온다.) UDP는 *"한 번 보내면 한 데이터그램"* 이라 **메시지 하나 = 데이터그램 하나**로 딱 맞아떨어진다. `Send`는 `sendto` 한 번, `Receive`는 `recvfrom` 한 번으로 데이터그램을 통째로 받는다. 논블로킹이라 받을 게 없으면 `nullopt`. (지금은 Windows 전용이다.)

정리하면, 경계를 긋는 방식은 통로마다 다르다.

| 통로 | 메시지 경계를 어떻게 아는가 |
|---|---|
| MemoryConnection | vector 단위 큐 (경계가 곧 vector) |
| UDPConnection | 데이터그램 자체가 경계 |
| (미구현) TCPConnection | 스트림이라 경계가 없음 → 내부에서 길이 접두를 붙여 직접 프레이밍 |

`NetworkManager`는 이 중 무엇이 꽂혀 있는지 **전혀 모른다.** 그냥 "메시지 한 통"을 보내고 받는다.

---

## 직렬화 도구: `PacketWriter` / `PacketReader`

이제 값을 바이트로 바꾸는 도구를 만든다. 규칙은 **이름이 붙은 숫자 필드는 전부 빅엔디안(네트워크 바이트 오더, 큰 바이트부터)** 이다.
(참고로 빅엔디안이 네트워크 표준이다) 

```cpp
void WriteUInt32(uint32_t value) {
    WriteUInt8((value >> 24) & 0xFF);
    WriteUInt8((value >> 16) & 0xFF);
    WriteUInt8((value >> 8)  & 0xFF);
    WriteUInt8( value        & 0xFF);
}
```

`value >> 24`는 "메모리의 첫 바이트"가 아니라 "값의 상위 8비트"를 뜻하는 **산술** 연산이라, 리틀엔디안 기계에서도 빅엔디안 기계에서도 결과가 같다. `memcpy`로 짰다면 기계마다 달랐을 것이다.

읽을 때는 그대로 거꾸로 조립한다. `PacketReader`는 `offset`을 들고 다니며 한 번 읽을 때마다 전진하고, 버퍼가 모자라면 `false`를 돌려준다. 그래서 디코딩 코드는 자연스럽게 `a && b && c` 식의 줄줄이 AND가 된다. 하나라도 모자라면 거기서 멈춘다.

`Fixed64`는 특별 취급한다.

```cpp
void WriteFixed64(const Fixed64& value) { WriteInt64(value.Raw()); }
```

여기서 선로에 나가는 것은 **raw 정수**다. `float`을 보내면 받는 쪽 CPU/컴파일러의 반올림이 미세하게 달라 시뮬레이션이 갈라질 수 있다. 고정소수점 raw 비트를 그대로 옮기면 서버와 클라가 **비트 단위로 동일한 값**을 본다. 따라서 결정론적인 게임을 가능하게 해준다.

> ⚠️ **다만 "항상 빅엔디안"이라고 하면 과장이다.** 빅엔디안이 보장되는 건 **타입을 명시해 쓰는 함수들**이다. `WriteUInt8/16/32/64`, `WriteInt*`, `WriteFloat`(비트를 `memcpy`로 꺼내 `WriteUInt32`에 넘긴다), `WriteFixed64`(`Raw()`를 `WriteInt64`에 넘긴다).
> 반면 `WriteBytes` · `WriteTrivial<T>` · `AutoWrite<T>`는 **객체 표현을 그대로 버퍼에 붙인다.** 바이트 오더 변환이 아예 없다. 그리고 `EncodeWorldSnapshot`이 컴포넌트 dense 바이트를 실을 때 쓰는 게 그 경로다(`writer.Bytes(component.bytes.data(), ...)`). 즉 **헤더와 이름 붙은 필드는 이식 가능하지만, 스냅샷 안의 컴포넌트 덩어리는 아니다.**
> 이건 버그라기보다 **아직 갚지 않은 전제**다. "양쪽이 같은 툴체인·같은 ABI로 빌드된다"에 기대고 있다. 이 가정을 깨려면 컴포넌트마다 필드 단위 직렬화를 써야 한다(맨 앞에서 본 패딩 문제도 같은 자리에 그대로 남아 있다).

---

## 패킷의 모양: 프레임 = 버전 + 레코드들

통로가 "메시지 한 통"의 경계는 책임져 준다. 그럼 `NetworkManager`는 그 메시지 안에 뭘 담을까? 이걸 **프레임(frame)** 이라 부르자. 프레임은 맨 앞에 버전 1바이트가 오고, 그 뒤로 패킷 **레코드**가 하나 이상 이어진다. 레코드 하나는 `[type | size | payload]`다.

```
한 프레임 (= 통로가 주는 메시지 한 통)
┌─────────┬─────────────────────────┬─────────────────────────┐
│ version │ type │ size │  payload  │ type │ size │  payload  │  ...
│  (1B)   │ (1B) │ (4B) │  (size)   │ (1B) │ (4B) │  (size)   │
└─────────┴─────────────────────────┴─────────────────────────┘
            └─────── 레코드 A ──────┘ └─────── 레코드 B ──────┘
```

- **version** 프레임당 1바이트. 첫 바이트가 우리 버전이 아니면 → 통째로 버린다. (설계 초안에서는 4바이트 magic(`EFSY`)을 앞에 붙일 생각이었는데, 그건 **초안에만 있었고 코드로 들어온 적은 없다.** UDP 쓰레기 거르기 + 포맷 버전 표시는 1바이트면 충분하다고 보고 magic 대신 version 1바이트를 채택했다.)
- **type** `PacketType` (`Hello`, `TypeTable`, `WorldSnapshot`, `InputCommand`, `TickHash`, `ResyncRequest` …).
- **size** 그 레코드의 본문 길이.

### 잠깐, 통로가 경계를 안다며? `size`가 왜 또 필요해?

좋은 질문이고, 이게 이 글에서 제일 중요한 통찰이다. **framing은 두 층이 있다.**

| 층 | 질문 | 누가 푸나 |
|---|---|---|
| **전송 framing** | 한 *메시지(프레임)* 는 어디서 끝나나? | 통로 (UDP 데이터그램 / 메모리 큐 / TCP 길이접두) |
| **프레임 내부 framing** | 한 프레임 *안* 에 패킷을 여러 개 담으면 각 패킷은 어디서 끝나나? | `NetworkManager`의 per-record **`size`** |

UDP 데이터그램 길이는 **바깥 봉투(프레임 전체)** 경계만 알려준다. 봉투 안에 패킷 A·B·C를 욱여넣으면, A가 어디서 끝나고 B가 어디서 시작하는지는 아무도 안 알려준다. 그걸 가르는 게 레코드의 `size`다.

그래서 지금은 **한 프레임에 패킷 하나(N=1)** 만 담아 보내지만 `size`를 유지한다. 나중에 *"한 틱에 보낼 패킷이 여러 개면 한 프레임에 묶어 보내기"*(배칭)를 켜는 순간, **같은 와이어 포맷 그대로** N개를 담을 수 있기 때문이다.

### 보내고 받기

보내는 쪽(`SendPacket`)은 프레임을 **하나의 버퍼로** 만들어 `Send` **한 번**으로 내보낸다.

```cpp
Writer writer;
writer.U8(kProtocolVersion);                       // 프레임당 1번
writer.U8(static_cast<uint8_t>(type));             // ┐
writer.U32(static_cast<uint32_t>(payload.size())); // │ 레코드 하나
auto frame = writer.Take();
frame.insert(frame.end(), payload.begin(), payload.end()); // ┘
m_connection->Send(frame);                         // 통째로 한 번
```

받는 쪽(`TryReceivePacket`)은 통로에서 프레임 한 통을 받아 버전을 확인하고, **버퍼 끝까지 레코드를 하나씩** 꺼낸다.

```cpp
// 현재 프레임을 다 읽었으면 다음 프레임을 통로에서 가져온다
if (m_frameOffset >= m_currentFrame.size()) {
    auto frame = m_connection->Receive();
    if (!frame) return std::nullopt;
    m_currentFrame = std::move(*frame);
    if (m_currentFrame[0] != kProtocolVersion) { /* 버리고 다음 프레임 */ }
    m_frameOffset = 1; // 버전 바이트 건너뛰기
}
// 레코드 하나 떼어내기: [type][size][payload]
Reader r(std::span(m_currentFrame).subspan(m_frameOffset));
r.U8(rawType); r.U32(size);
// payload는 (m_frameOffset + 5)부터 size 바이트
```

`packet count`가 따로 필요 없다는 게 포인트다. 통로가 프레임 경계를 보장하니, **끝에 닿을 때까지 레코드를 읽으면** 그게 곧 패킷 개수다.

> ⚠️ **아직 남은 한계.** 메시지 경계가 깔끔해졌다고 *신뢰성* 까지 생긴 건 아니다. UDP 메시지는 여전히 유실·순서뒤바뀜·중복이 가능하다(`Packet.hpp`의 `sequenceNumber`/`ackNumber`/`ReliablePacket`은 이 신뢰성 계층을 위한 빈자리다). 또 한 데이터그램(~1400B)을 넘는 큰 메시지(월드 스냅샷)는 분할/재조립이 필요하고, 틱 단위 배칭(Queue/Flush)은 포맷만 준비된 상태이다.

---

## `NetworkManager`

이제 통로(`INetworkConnection`)와 포장 규칙(패킷)을 하나로 묶는 게 `NetworkManager`다. 월드에 리소스로 붙일 물건이다.

```cpp
world->AddResource<NetworkManager>(connection);
```

> 시점 주의: 이건 **의도한 배치이고, 05 커밋의 코드는 여기까지 오지 않았다.** 05 시점(`bf71ab2`)의 트리 전체에서 `AddResource<NetworkManager>`를 부르는 제품 코드는 한 줄도 없고, `NetworkManager`를 실제로 만들어 쓰는 것은 `tests/NetworkTests.cpp` 하나다. 월드 리소스로 붙는 배선은 다음 커밋 `0cf2063`에서 들어왔다.

역할은 두 가지다.

### 1. 패킷 종류별 Send / Encode / Decode

각 패킷 타입마다 짝이 맞는 인코더/디코더가 있다. 예를 들어 `InputCommand`는 이렇게 평평하게 펼친다.

```cpp
std::vector<uint8_t> EncodeInputCommand(const InputCommand& c) {
    Writer w;
    w.U32(c.clientId);
    w.U64(c.tick);
    w.U32(c.sequence);
    w.U64(c.buttonMask);
    w.WriteFixed64(c.aimYaw);
    w.WriteFixed64(c.aimPitch);
    return w.Take();
}

bool DecodeInputCommand(const std::vector<uint8_t>& payload, InputCommand& out) {
    Reader r(payload);
    return r.U32(out.clientId) && r.U64(out.tick) && r.U32(out.sequence)
        && r.U64(out.buttonMask) && r.ReadFixed64(out.aimYaw) && r.ReadFixed64(out.aimPitch)
        && r.Finished(); // 남는 바이트 없이 딱 떨어져야 정상
}
```

보내는 함수들은 전부 같은 패턴이다. 페이로드를 `Writer`로 만들고 `SendPacket(type, payload)`에 넘긴다. 받는 쪽은 `TryReceivePacket`으로 한 통 꺼낸 뒤 `packet.type`을 보고 알맞은 `Decode`를 부른다.

### 2. 클라이언트별 상태 관리 (서버 입장)

서버는 여러 클라를 동시에 상대한다. 그래서 `clientId`마다 상태를 따로 둔다.

```cpp
struct NetworkClientState {
    uint32_t clientId;
    uint64_t lastReceivedTick;
    bool hasTypeTable;
    bool hasSnapshot;
    std::vector<ComponentTypeDescriptor> remoteTypes;
    std::deque<InputCommand> pendingInputs; // 수락된 입력이 "도착 순서대로" 쌓이는 큐
    uint32_t lastInputSequence;             // 마지막으로 수락한 입력 시퀀스
    bool hasInput;
};
```

클라가 보낸 `InputCommand`는 `EnqueueInput`으로 해당 클라의 `pendingInputs`에 쌓인다. 이때 **시퀀스 번호**로 거른다. UDP에선 같은 입력이 중복으로 오거나 순서가 뒤바뀌어 올 수 있어서, 마지막으로 수락한 시퀀스보다 작거나 같으면(중복·구식) 버린다.

```cpp
bool EnqueueInput(const InputCommand& cmd) {
    auto& state = GetOrCreateClient(cmd.clientId);
    // 부호 있는 차이로 비교해 32비트 wraparound도 안전하게.
    if (state.hasInput &&
        static_cast<int32_t>(cmd.sequence - state.lastInputSequence) <= 0)
        return false; // 중복이거나 과거 입력 → 폐기
    state.hasInput = true;
    state.lastInputSequence = cmd.sequence;
    state.lastReceivedTick = cmd.tick;   // 기록만 한다 — 단조 증가는 검사하지 않는다
    state.pendingInputs.push_back(cmd);
    return true;
}
```

> ⚠️ **이 큐는 "tick 순서 큐"라기보다 "도착 순서 큐"다.** `EnqueueInput`이 검사하는 건 **`sequence`뿐**이고, `tick`은 `lastReceivedTick`에 기록만 할 뿐 단조 증가를 확인하지 않는다. 그래서 *sequence는 늘어나는데 tick은 줄어드는* 입력을 넣어도 그대로 `push_back`된다. 엄밀히 쓰면 **"sequence 기준으로 새것이라 판정된 입력이 도착 순서대로 쌓이는 큐"** 다.

그리고 이 시점의 공개 API에는 **큐를 소비하는 함수가 없다.** 들여다보는 `PendingInputs(clientId)` 하나만 있다. 그러니까 *"서버가 자기 tick에 맞춰 하나씩 꺼내 시뮬레이션에 먹인다"* 는 아직 그림에 머물러 있고 코드로는 없다. 이 자리를 나중에 lockstep 편이 `inputByTick` 슬롯 맵 + `ConfirmInputs(tick)`으로 메운다. 그때 자료 구조가 *큐에서 tick 키 맵으로* 바뀌는 게 핵심인데, 이유는 이렇다. 결정론 lockstep이 요구하는 건 언제나 **"tick N의 것"** 이지 "다음 것"이 아니기 때문이다. (클라가 시퀀스를 매겨 보내는 쪽도 마찬가지로 추후 입력 시스템에서 구현할 예정.)

> 헷갈리지 말 것: 여기서 쓰는 `InputCommand`의 **시퀀스**(애플리케이션 레벨, 입력 순서)와, `Packet.hpp`의 `PacketHeader.sequenceNumber`/`ackNumber`(**전송 레벨**, 신뢰성·재전송용)는 다른 개념이다. 후자는 아직 빈자리다.

---

## 전체 그림: 패킷이 만드는 대화

7종의 패킷이 결국 만들려는 흐름은 이렇다.

1. `Hello`: 클라가 접속을 알린다.
2. `TypeTable`: 서버가 "내 월드엔 이런 컴포넌트 타입들이 있다"(hash/size/alignment)를 알려준다.
3. `WorldSnapshotRequest` → `WorldSnapshot`: 클라가 현재 월드 상태를 통째로 받아 자신을 맞춘다.
4. 이후 매 tick: 클라는 `InputCommand`를, 서버는 `TickHash`(tick + 월드 해시)를 주고받는다.
5. 해시가 어긋나면 `ResyncRequest` → 서버가 다시 `WorldSnapshot`을 보내 강제로 맞춘다.

> ⚠️ **이 대화는 05 시점에 아직 돌지 않는다.** 위 순서를 "05가 실제로 하는 일"로 읽으면 틀린다. 05 시점(`bf71ab2`)에서 `SendHello`·`TryReceivePacket`·`EnqueueInput`을 부르는 곳은 **`tests/NetworkTests.cpp`뿐**이고, 이 순서를 fixed tick 위에서 굴리는 시스템은 없다. 05가 만든 것은 *패킷 종류와 각각의 인코더·디코더*까지다. 이걸 상태 머신으로 엮는 `NetworkSyncSystem`은 다음 글(06)에서 온다.
>
> 특히 1번 다음에 당연히 있어야 할 "서버가 클라에게 `clientId`를 발급한다"는, **05 시점에 실어 보낼 패킷 자체가 없다.** 위 7종에는 서버가 "네 id는 7이다"라고 말할 수단이 없고, 그 역할의 `Welcome`(= 8번)은 06에서야 `PacketType`에 추가된다. 05에서 `clientId`는 `SetLocalClientId`로 *바깥에서 꽂아 넣는* 값이다. 그러니 이 목록은 **05가 겨냥한 목적지**로 읽는 게 맞다.

이 패킷들이 하려는 일은 결국 하나다 — **서버가 권위를 갖고, 클라는 그 상태에 자신을 맞추게 하는 것.**

---

## 마무리

이 글에선 *"바이트를 어떻게 규칙 있게 주고받는가"* 까지를 다뤘다.

- `INetworkConnection`을 **메시지 파이프**로 추상화해, 메시지 경계는 각 통로(메모리 큐 / UDP 데이터그램 / 미래의 TCP 길이접두)가 책임지게 하고,
- `PacketWriter`/`Reader`로 **이름 붙은 숫자 필드의 엔디안과 Fixed64 raw를 직접 직렬화**하고(컴포넌트 dense 바이트는 아직 raw 그대로다),
- 프레임 = `version + [type|size|payload]…` 으로 **한 프레임에 패킷 여러 개**를 담을 수 있게(지금은 N=1) 잡고,
- `NetworkManager`가 이 모든 것을 관리한다.

아직 부족한 것은 UDP 신뢰성(유실·순서·중복), 큰 메시지 분할, 그리고 틱 단위 배칭이다.

우선 이정도면 통신부분의 기능은 구현이 됐기 때문에 다음 단계로 넘어갈 것 같다.


