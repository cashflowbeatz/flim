import { Component, createRef, type ErrorInfo, type ReactNode } from 'react';

export default class ErrorBoundary extends Component<{children: ReactNode}, {failed: boolean; logsError: string; opening: boolean}> {
  state = {failed: false, logsError: '', opening: false};
  private surface = createRef<HTMLElement>();
  private heading = createRef<HTMLHeadingElement>();
  private openingLogs = false;
  private mounted = false;
  private observer?: ResizeObserver;

  componentDidMount() { this.mounted = true; }
  componentWillUnmount() { this.mounted = false; this.observer?.disconnect(); }

  static getDerivedStateFromError() { return {failed: true}; }

  componentDidCatch(error: Error, info: ErrorInfo) {
    try { window.flimDesktop?.reportError({message: error.message, stack: `${error.stack || ''}\n${info.componentStack || ''}`}); } catch {}
    this.heading.current?.focus();
    this.fitRecovery();
    if (this.surface.current && typeof ResizeObserver !== 'undefined') {
      this.observer?.disconnect();
      this.observer = new ResizeObserver(() => this.fitRecovery());
      this.observer.observe(this.surface.current);
    }
  }

  componentDidUpdate() { if (this.state.failed) this.fitRecovery(); }

  private fitRecovery() {
    try { if (this.surface.current) window.flimDesktop?.fitWindow?.(Math.ceil(this.surface.current.getBoundingClientRect().height)); } catch {}
  }

  render() {
    if (!this.state.failed) return this.props.children;
    return <main ref={this.surface} className="app-shell recovery">
      <h1>flim</h1><h2 ref={this.heading} tabIndex={-1}>The interface needs to reload.</h2>
      <p>This reloads the room controls. It leaves FL Studio and the current room running.</p>
      <button className="primary" onClick={() => window.location.reload()}>Reload interface</button>
      {window.flimDesktop && <button className="text-button" disabled={this.state.opening} onClick={async () => {
        if (this.openingLogs) return;
        this.openingLogs = true;
        this.setState({opening: true, logsError: ''});
        try { await window.flimDesktop!.openLogs(); }
        catch { if (this.mounted) this.setState({logsError: 'The test logs folder could not be opened.'}); }
        finally { this.openingLogs = false; if (this.mounted) this.setState({opening: false}); }
      }}>{this.state.opening ? 'Opening test logs…' : 'Open test logs'}</button>}
      {this.state.logsError && <p className="error" role="alert">{this.state.logsError}</p>}
    </main>;
  }
}
