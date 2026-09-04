import express, { type Request, type Response } from "express";

const app = express();
const host = "0.0.0.0";
const port = Number(process.env.PORT ?? 8080);

app.get("/api/status", (_req: Request, res: Response) => {
  res.json({
    message: "Hello from Raspberry Pi",
  });
});

app.listen(port, host, () => {
  console.log(`Infhome Raspberry API listening on http://${host}:${port}`);
});
