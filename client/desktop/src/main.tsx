import ReactDOM from 'react-dom/client';
import App from './App';
import ErrorBoundary from './ErrorBoundary';
import './tokens.css';
import './styles.css';

function report(error: unknown) {
  try { window.flimDesktop?.reportError(error instanceof Error ? {message: error.message, stack: error.stack} : {message: String(error)}); } catch {}
}
window.addEventListener('error', event => report(event.error ?? event.message));
window.addEventListener('unhandledrejection', event => report(event.reason));

ReactDOM.createRoot(document.getElementById('root')!, {onRecoverableError: report}).render(<ErrorBoundary><App /></ErrorBoundary>);
