# Kolibri New Modules Overview

## Summary

This document describes the new modules added to the Kolibri AGI system, enhancing reasoning capabilities, intent classification, and reinforcement learning-based autonomous decision making.

---

## 1. Enhanced Reasoning Engine

### Location
- Header: `backend/include/kolibri/reasoning_engine.h`
- Implementation: `backend/src/reasoning_engine.c`

### New Logical Inference Rules

#### 1.1 Hypothetical Syllogism
**Rule**: P→Q, Q→R ⊢ P→R

If P implies Q, and Q implies R, then P implies R.

```c
KolibriReasoningResult result;
kolibri_re_hypothetical_syllogism(
    "дождь -> мокро",
    "мокро -> скользко",
    &config, &result
);
// Result: "По гипотетическому силлогизму: если дождь, то скользко"
```

**Use Cases**:
- Causal chain reasoning
- Transitive relationships
- Policy implications

#### 1.2 Constructive Dilemma
**Rule**: (P→Q)∧(R→S), P∨R ⊢ Q∨S

Given two implications and a disjunction of their antecedents, derive disjunction of consequents.

```c
kolibri_re_constructive_dilemma(
    "учиться -> знать",
    "практиковаться -> уметь",
    "учиться или практиковаться",
    &config, &result
);
```

**Use Cases**:
- Decision making with alternatives
- Strategic planning
- Resource allocation

#### 1.3 Disjunctive Syllogism
**Rule**: P∨Q, ¬P ⊢ Q

Given a disjunction and negation of one disjunct, derive the other.

```c
kolibri_re_disjunctive_syllogism(
    "идет дождь или снег",
    "не идет дождь",
    &config, &result
);
// Result: "Идет снег"
```

**Use Cases**:
- Elimination reasoning
- Diagnostic troubleshooting
- Process of elimination

#### 1.4 Resolution
**Rule**: P∨Q, ¬P∨R ⊢ Q∨R

Fundamental inference rule used in automated theorem proving and SAT solvers.

```c
kolibri_re_resolution(
    "P или Q",
    "не P или R",
    &config, &result
);
// Result: "Q или R"
```

**Use Cases**:
- Automated theorem proving
- Constraint satisfaction
- Logic puzzle solving

#### 1.5 Biconditional Reasoning
**Rule**: P↔Q, P ⊢ Q (and vice versa)

If P if and only if Q, and P is true, then Q is true.

```c
kolibri_re_biconditional(
    "P тогда и только тогда, когда Q",
    "P",
    &config, &result
);
```

**Use Cases**:
- Equivalence proofs
- Definition-based reasoning
- Mathematical reasoning

### New Logical Operators

Added to `KolibriLogicalOp` enum:
- `KRE_OP_XOR` - Exclusive OR
- `KRE_OP_NAND` - Sheffer stroke (NOT AND)
- `KRE_OP_NOR` - Peirce arrow (NOT OR)

### New Reasoning Types

Added to `KolibriReasoningType` enum:
- `KRE_REASONING_HYPOTHETICAL_SYLLOGISM` (5)
- `KRE_REASONING_CONSTRUCTIVE_DILEMMA` (6)
- `KRE_REASONING_DISJUNCTIVE_SYLLOGISM` (7)
- `KRE_REASONING_RESOLUTION` (8)
- `KRE_REASONING_BICONDITIONAL` (9)

---

## 2. Intent Classifier Module

### Location
- Header: `backend/include/kolibri/intent_classifier.h`
- Implementation: `backend/src/intent_classifier.c`

### Purpose

Classifies user queries into intent categories to select appropriate processing strategies.

### Supported Intents

| Intent | Description | Example |
|--------|-------------|---------|
| `QUERY_FACT` | Factual query | "сколько будет 2+2?" |
| `QUERY_DEFINITION` | Definition request | "что такое квантовая физика?" |
| `QUERY_COMPARISON` | Comparison | "разница между Python и C" |
| `QUERY_CAUSE` | Cause/Reason | "почему небо голубое?" |
| `QUERY_PROCESS` | Process explanation | "как работает двигатель" |
| `LOGIC_PUZZLE` | Logic puzzle | "реши логическую задачу" |
| `MATH_PROBLEM` | Math problem | "вычисли интеграл" |
| `EXPLAIN` | Explanation request | "объясни теорию относительности" |
| `EXAMPLE` | Request for example | "приведи пример" |
| `COUNTERFACTUAL` | Counterfactual | "что если бы Земля была плоской?" |
| `TEACH` | Teaching/learning | "научи меня Python" |
| `GREETING` | Greeting | "привет!" |
| `FAREWELL` | Farewell | "до свидания" |
| `THANKS` | Thanks | "спасибо!" |

### API Usage

```c
KolibriIntentClassifier classifier;
kolibri_ic_init(&classifier);

KolibriIntentResult result;
kolibri_ic_classify(&classifier, "что такое AI?", &result);

printf("Intent: %s\n", kolibri_ic_intent_name(result.primary_intent));
printf("Confidence: %.2f\n", result.confidence);
printf("Requires Reasoning: %s\n", result.requires_reasoning ? "Yes" : "No");
printf("Recommended Method: %s\n", result.recommended_method);

kolibri_ic_destroy(&classifier);
```

### Features

- **Fast classification**: `kolibri_ic_classify_fast()` for quick intent detection
- **Top-N hypotheses**: Returns multiple intent hypotheses with confidence scores
- **Metadata extraction**: 
  - Domain detection (math, science, logic, etc.)
  - Query complexity estimation
  - Entity extraction
- **Recommended processing method**: Suggests best handling strategy
- **Pattern extensibility**: Add custom patterns with `kolibri_ic_add_pattern()`

### Performance

- Average classification time: < 100µs
- Supports 256 custom patterns
- Minimum confidence threshold: 0.6

---

## 3. Reinforcement Learning Module

### Location
- Header: `backend/include/kolibri/reinforcement_learning.h`
- Implementation: `backend/src/reinforcement_learning.c`

### Purpose

Q-learning based action selection system that learns optimal processing strategies based on reward signals.

### Architecture

```
┌─────────────────────────────────────────────────────┐
│                State Encoder                          │
│  (Intent, Complexity, Domain, Requirements)          │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│              Q-Table (1024 states)                   │
│  State → [Q(action1), Q(action2), ... Q(action8)]   │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│         Epsilon-Greedy Action Selection              │
│  Exploration vs Exploitation balance                 │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
              Selected Action
```

### Available Actions

| Action | Description |
|--------|-------------|
| `USE_KNOWLEDGE_BASE` | Answer from knowledge base |
| `USE_REASONING` | Logical inference |
| `USE_MATH_SOLVER` | Mathematical solver |
| `USE_FORMULA_POOL` | Formula-based answers |
| `USE_WORLD_MODEL` | Neural text generation |
| `USE_ANALOGY` | Analogy-based reasoning |
| `USE_COUNTERFACTUAL` | Counterfactual reasoning |
| `FALLBACK_CHAT` | Fallback to external LLM |

### Reward Function

```
reward = confidence * 0.35 + 
         time_score * 0.2 + 
         user_feedback * 0.25 + 
         intent_match * 0.2
```

Components:
- **Confidence** (35%): System confidence in answer
- **Response Time** (20%): Speed of response (<100ms = 1.0, >1000ms = 0.1)
- **User Feedback** (25%): Explicit user feedback (-1 to +1)
- **Intent Match** (20%): Whether selected action matched intent

### API Usage

```c
KolibriRLContext rl_ctx;
kolibri_rl_init(&rl_ctx);

// Create state from intent
KolibriRLState state;
state.intent = KIC_INTENT_MATH_PROBLEM;
state.complexity = 0.8;
state.requires_reasoning = 1;
state.hash = kolibri_rl_hash_state(&state);

// Select action
KolibriRLAction action;
kolibri_rl_select_action(&rl_ctx, &state, &action);
printf("Selected: %s\n", kolibri_rl_action_name(action));

// After getting answer, update with reward
double reward = kolibri_rl_compute_reward(
    0.9,    // confidence
    150.0,  // response time (ms)
    1.0,    // user feedback
    1       // intent matched
);

kolibri_rl_update(&rl_ctx, &state, action, reward, &next_state);

// Save learned Q-table
kolibri_rl_save_qtable(&rl_ctx, "qtable.bin");

kolibri_rl_destroy(&rl_ctx);
```

### Experience Replay

Stores experiences for batch training:

```c
KolibriExperience exp;
exp.state = state;
exp.action = action;
exp.reward = 0.85;
exp.next_state = next_state;
exp.done = 1;

kolibri_rl_store_experience(&rl_ctx, &exp);

// Replay mini-batch
kolibri_rl_replay(&rl_ctx, 32);
```

### Features

- **Q-learning**: Classic Q-learning algorithm
- **Experience Replay**: 10,000 experience buffer
- **Epsilon Decay**: Automatic exploration rate decay (0.1 → 0.01)
- **Persistency**: Save/load Q-table to disk
- **Statistics**: Comprehensive tracking of learning progress
- **Thread-safe**: Mutex-protected Q-table access

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `alpha` | 0.001 | Learning rate |
| `gamma` | 0.99 | Discount factor |
| `epsilon` | 0.1 | Exploration rate |
| `epsilon_min` | 0.01 | Minimum exploration rate |
| `epsilon_decay` | 0.995 | Decay rate per update |

---

## 4. Integration

### CMakeLists.txt

New modules added:
- `backend/src/intent_classifier.c`
- `backend/src/reinforcement_learning.c`

New tests:
- `tests/test_reasoning_engine.c`
- `tests/test_intent_classifier.c`
- `tests/test_reinforcement_learning.c`

### Build & Test

```bash
# Build
cmake -B build -DKOLIBRI_ENABLE_TESTS=ON
cmake --build build

# Run new tests
cd build
ctest -R test_reasoning_engine --verbose
ctest -R test_intent_classifier --verbose
ctest -R test_reinforcement_learning --verbose
```

---

## 5. Future Work

### Reasoning Engine
- [ ] Implement temporal logic reasoning
- [ ] Add probabilistic reasoning (Bayesian networks)
- [ ] Support for modal logic (necessity, possibility)
- [ ] Integration with world model for grounded reasoning

### Intent Classifier
- [ ] ML-based classification (neural network)
- [ ] Multi-language support
- [ ] Context-aware intent tracking
- [ ] Emotion/intent sentiment analysis

### Reinforcement Learning
- [ ] Deep Q-Network (DQN) implementation
- [ ] Policy gradient methods
- [ ] Multi-agent RL for swarm learning
- [ ] Transfer learning between domains

---

## 6. References

- Russell, S. & Norvig, P. "Artificial Intelligence: A Modern Approach" - Logic & Reasoning
- Sutton, R.S. & Barto, A.G. "Reinforcement Learning: An Introduction" - Q-Learning
- Jurafsky, D. & Martin, J.H. "Speech and Language Processing" - Intent Classification

---

## Copyright

Copyright (c) 2025 Кочуров Владислав Евгеньевич
