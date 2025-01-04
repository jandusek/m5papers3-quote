# m5paper-quote

Displays random quotes on an [M5 PaperS3](https://docs.m5stack.com/en/core/PaperS3) E-ink device.

This assumes existence of a REST API (for example [this one](https://github.com/jandusek/quote-api)) that returns quotes in the following format:

```json
{
  "id": 35,
  "rating": 5,
  "quote": "If you're struggling to understand someone's behavior or motivation, understand that it's almost always because of money, fear, or both.",
  "followup": null,
  "author": "Merlin Mann",
  "context": null
}
```

## Installation

1. `cp env.h.sample env.h`
2. Edit variables in `env.h`
3. Use Arduino IDE to compile and flash

## Example

![Example](example.jpeg)
