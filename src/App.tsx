import { useState } from "react";
import Boot from "./components/Boot";
import { Nav, Marquee, Footer } from "./components/Chrome";
import Hero from "./components/Hero";
import BranchReview from "./components/BranchReview";
import Arcade from "./components/Arcade";
import Architecture from "./components/Architecture";

export default function App() {
  const [booted, setBooted] = useState(false);

  return (
    <div id="top" className="relative min-h-screen bg-void text-ink">
      {!booted && <Boot onDone={() => setBooted(true)} />}
      <div className="noise-layer" />
      <div className="scanline-beam" />

      <Nav />
      <main>
        <Hero />
        <Marquee />
        <BranchReview />
        <div className="mx-auto h-px max-w-6xl bg-gradient-to-r from-transparent via-hairline to-transparent" />
        <Arcade />
        <div className="mx-auto h-px max-w-6xl bg-gradient-to-r from-transparent via-hairline to-transparent" />
        <Architecture />
      </main>
      <Footer />
    </div>
  );
}
