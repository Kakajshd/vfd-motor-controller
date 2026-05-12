# Copilot Instructions (Advanced - Production Ready)

## Language Priority
- ALWAYS respond in Vietnamese for explanations, reasoning, and communication.
- Use English ONLY for:
  + Code
  + Variable/function names
  + Technical keywords when necessary
- Do NOT respond fully in English unless explicitly requested.

---

## General Coding Principles
- Always write production-ready code.
- Avoid unnecessary explanations unless explicitly requested.
- Prefer correctness, robustness, and maintainability over brevity.
- Avoid placeholders or pseudo-code unless asked.
- Always provide FULL working code when generating code.

---

## Coding Style
- No emojis in code.
- No decorative comments.
- Only meaningful comments when necessary.
- Use clear, descriptive naming (no abbreviations unless standard).
- Follow consistent formatting.

---

## Architecture Rules
- Prefer modular design (split into functions/modules/files).
- Avoid monolithic code.
- Clearly separate:
  + Hardware layer
  + Logic layer
  + Communication layer
  + UI layer (if any)

---

## Embedded Systems (ESP32 / MCU)
- STRICTLY avoid delay()
- Use non-blocking programming
- Use:
  + millis()
  + timers
  + scheduler pattern
  + state machine

- Ensure real-time responsiveness
- Avoid blocking loops
- Optimize RAM and stack usage
- Avoid dynamic allocation if not necessary

---

## Performance Optimization
- Minimize CPU usage
- Avoid unnecessary recalculations
- Cache values when possible
- Use efficient data structures
- Avoid redundant Serial prints in production code

---

## Communication (UART / RS485 / Modbus / I2C / SPI)
- Always design for reliability:
  + timeout handling
  + retry mechanism
  + error checking

- Avoid spamming communication
- Use rate limiting when sending commands
- Validate all received data

---


## Error Handling
- Always handle edge cases
- Never assume ideal input
- Add fallback logic
- Add debug hooks (optional, removable)

---

## Code Behavior Expectations
When generating code:
- Provide COMPLETE code (not partial snippets)
- Ensure code compiles
- Ensure logic is consistent
- Avoid breaking existing structure unless asked

---

## Refactoring Rules
When asked to improve code:
- Keep original logic intact unless flawed
- Improve:
  + readability
  + performance
  + structure
- Clearly separate BEFORE vs AFTER if needed

---

## UI / Web / HMI (if applicable)
- Clean, industrial style (HMI-like)
- Minimalist design
- Focus on clarity and usability
- Avoid flashy or unnecessary effects

---

## Response Behavior
- Be concise but precise
- Focus on actionable solutions
- Do NOT over-explain
- Do NOT repeat obvious information

---

## Special Priority
- Stability > Performance > Readability
- Real-world applicability over theory
- Production-ready output is mandatory