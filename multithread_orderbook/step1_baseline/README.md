# Step 1: Single-Threaded Order Book

## 🎯 이 단계의 목표

> **"Order Book의 핵심 로직을 정확하게 구현했는가?"**

- ✅ Price-Time Priority 준수
- ✅ Limit/Market Order 지원
- ✅ Cancel 기능
- ❌ Thread safety (아직 없음 - Step 2에서 추가)

## 📚 이 단계에서 배울 수 있는 것들

### 1. 자료구조 설계
- **`std::map`**: 가격 정렬이 자동으로 되는 이유
- **`std::list`**: 같은 가격 내 FIFO + 포인터 안정성 (재할당 없음)
- **`std::unordered_map`**: 빠른 Order ID 조회 (O(1))

### 2. 알고리즘
- **Matching engine**: Market order가 Limit order와 체결되는 로직
- **Best price 유지**: map의 특성을 활용한 O(1) 조회

### 3. C++ 기본 문법
- `std::optional` 사용법 (값이 없을 수 있는 경우)
- range-based for loop
- lambda 함수

## 🏗️ 코드 구조

```
Order (struct)
  ├─ id, symbol_id
  ├─ type (LIMIT/MARKET)
  ├─ side (BUY/SELL)
  └─ price, quantity, remaining

OrderBook (class)
  ├─ bids_ : map<price, list<Order>>      // 매수 주문
  ├─ asks_ : map<price, list<Order>>      // 매도 주문
  └─ orders_ : unordered_map<id, Order*>  // 빠른 조회용
```

### Price-Time Priority 구현

```
매수(BID) 정렬: 높은 가격 우선
  Price 102: [Order1, Order2]  ← 최선 가격
  Price 101: [Order3]
  Price 100: [Order4, Order5]

매도(ASK) 정렬: 낮은 가격 우선
  Price 103: [Order6, Order7]  ← 최선 가격
  Price 104: [Order8]
  Price 105: [Order9]
```

## 🧪 테스트 실행

```bash
cd step1_baseline
mkdir build && cd build
cmake ..
make
./test_correctness
```

**예상 출력:**
```
========================================
Step 1: Correctness Tests
========================================

[TEST] Add Limit Order
  ✓ Best bid: 100
  ✓ Best ask: 110
  PASSED

[TEST] Price-Time Priority
  ✓ Time priority maintained
  PASSED

...

All tests PASSED! ✓
```

## 🚨 알려진 제한사항 (의도적)

### 1. Thread-unsafe
- **현상**: 여러 스레드가 동시에 접근하면 race condition
- **해결**: Step 2에서 mutex 추가

### 2. Cancel이 O(n)
- **현상**: `list`에서 선형 탐색
- **이유**: 프로파일링 결과 병목이 아님 (lock contention이 병목)
- **면접 답변**: "알고 있지만, 우선순위를 lock 최적화에 뒀습니다"

### 3. ~~Order 포인터 불안정성~~ ✅ 해결됨
- **이전 문제**: `std::vector` 사용 시 reallocation으로 포인터 무효화
- **해결**: `std::list` 사용으로 포인터 안정성 확보
- **Trade-off**: Cache locality는 떨어지지만, 포인터 안정성이 더 중요

## 💡 면접 대비 질문

### Q1: 왜 `std::map`을 선택했나요?
**A**: 가격은 정렬이 필요합니다. `map`은 자동으로 키를 정렬하고, best price 조회가 O(1)입니다 (`begin()` 또는 `rbegin()`).

### Q2: `std::unordered_map`은 왜 안 썼나요?
**A**: Price level에는 정렬이 필수입니다. Hash map은 순서가 없어요. Order ID 조회용으로만 unordered_map을 사용했습니다.

### Q3: `std::vector`가 아닌 `std::list`를 쓴 이유는?
**A**: price level 컨테이너에 포인터를 저장하기 때문에, 재할당 없는 컨테이너가 필요합니다. vector는 확장 시 포인터가 무효화되어 dangling pointer 위험이 있습니다. list는 포인터가 안정적이라 안전합니다.

### Q3: Cancel이 느린데 최적화 안 하나요?
**A**: 맞습니다, O(n)이에요. 하지만 프로파일링 결과 전체 연산의 5%였고, Step 2/3에서 진짜 병목인 lock contention에 집중했습니다.

### Q4: Market order가 부분 체결될 수 있나요?
**A**: 네, Limit order가 부족하면 체결 가능한 만큼만 체결되고, 나머지는 취소됩니다. 이것이 Market order의 특성입니다.

### Q5: Price-Time Priority를 어떻게 보장하나요?
**A**: 
1. Price: `std::map`의 정렬
2. Time: `list::push_back()`으로 끝에 추가 (FIFO)
3. 매칭 시 `list`를 앞에서부터 순회

### Q6: 왜 `std::list`를 사용했나요? `std::vector`가 더 빠른데요?
**A**: 포인터 안정성이 핵심입니다. `orders_`에 `Order*`를 저장하는데, `std::vector`는 재할당 시 모든 포인터가 무효화됩니다. `std::list`는 재할당이 없어 포인터가 안정적입니다.

**Trade-off:**
- `std::vector`: Cache locality 좋음, 하지만 포인터 불안정
- `std::list`: 포인터 안정적, 하지만 cache locality 떨어짐

CV 프로젝트에서는 **안정성**이 우선이므로 `std::list`를 선택했습니다.

## 📖 코드 읽는 순서 (학습용)

1. **`include/order.h`**: Order의 정의 (가장 기본)
2. **`include/order_book.h`**: OrderBook의 인터페이스와 자료구조
3. **`src/order_book.cpp`**: 
   - `add_limit_order()`: 주문 추가
   - `match_market_order()`: 매칭 로직 (핵심!)
   - `cancel_order()`: 취소
4. **`tests/test_correctness.cpp`**: 동작 확인

## 🎓 다음 단계

Step 2에서는:
- `std::mutex` 추가 → thread-safe하게 만들기
- 멀티스레드 벤치마크
- **문제 발견**: throughput이 스레드 수에 비례하지 않음 (lock contention)

---

**핵심 메시지**: "저는 Order Book의 핵심 개념을 처음부터 구현할 수 있습니다."

