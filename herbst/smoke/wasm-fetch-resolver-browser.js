const { chromium } = require("playwright");

(async () => {
  const url = process.argv[2] || "http://localhost:8095/wasmFetchResolver.html";
  const executablePath =
    process.env.CHROME_EXECUTABLE ||
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";

  const browser = await chromium.launch({
    executablePath,
    headless: true,
    args: ["--disable-gpu"],
  });
  const page = await browser.newPage();
  const messages = [];
  const errors = [];

  page.on("console", (msg) => messages.push(`${msg.type()}: ${msg.text()}`));
  page.on("pageerror", (err) => errors.push(err.stack || String(err)));

  await page.goto(url, { waitUntil: "networkidle", timeout: 30000 });
  await page.waitForFunction(
    () => globalThis.Module && Module.calledRun === true && typeof Module.ShowTree === "function",
    null,
    { timeout: 30000 },
  );

  const options = await page
    .locator("#input option")
    .evaluateAll((items) => items.map((option) => ({
      text: option.textContent,
      value: option.value,
    })));

  await page.click("#usdTree");
  await page.waitForFunction(() => {
    const text = document.querySelector("#output")?.textContent || "";
    return text && text !== "Generating..." && text.includes("/");
  }, null, { timeout: 30000 });
  const tree = await page.locator("#output").textContent();

  await page.click("#computeAllDependencies");
  await page.waitForFunction(() => {
    const text = document.querySelector("#output")?.textContent || "";
    return text && text !== "Computing Dependencies..." && text.length > 0;
  }, null, { timeout: 30000 });
  const dependencies = await page.locator("#output").textContent();

  await browser.close();

  if (errors.length) {
    throw new Error(`Page errors:\n${errors.join("\n")}`);
  }

  console.log(JSON.stringify({
    ok: true,
    selected: options[0],
    optionCount: options.length,
    treePreview: tree.slice(0, 300),
    dependenciesPreview: dependencies.slice(0, 300),
    console: messages,
  }, null, 2));
})();
