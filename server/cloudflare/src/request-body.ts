export class BodyReadError extends Error {
  constructor(readonly status: 400 | 408 | 413, message: string) { super(message); }
}

export async function boundedText(request: Request, max: number, timeoutMs = 10_000): Promise<string> {
  const declared = request.headers.get("content-length");
  if (declared && /^\d+$/.test(declared) && Number(declared) > max)
    throw new BodyReadError(413, "Request body is too large.");
  if (!request.body) return "";
  const reader = request.body.getReader(), decoder = new TextDecoder();
  let bytes = 0, text = "", timer: ReturnType<typeof setTimeout> | undefined;
  const expired = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new BodyReadError(408, "Request body timed out. Try again.")), timeoutMs);
  });
  try {
    for (;;) {
      const { value, done } = await Promise.race([reader.read(), expired]);
      if (done) return text + decoder.decode();
      bytes += value.byteLength;
      if (bytes > max) throw new BodyReadError(413, "Request body is too large.");
      text += decoder.decode(value, { stream: true });
    }
  } catch (error) {
    // A disconnected or stalled sender must not hold the response open while cancellation waits.
    void reader.cancel().catch(() => {});
    if (error instanceof BodyReadError) throw error;
    throw new BodyReadError(400, "Request body was interrupted. Try again.");
  } finally {
    if (timer !== undefined) clearTimeout(timer);
    reader.releaseLock();
  }
}
