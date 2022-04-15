import React from 'react';
import ReactDOM from 'react-dom';
import './index.css';
import CollabWrapper from './CollabWrapper';
import { BrowserRouter } from 'react-router-dom';

ReactDOM.render(
  <React.StrictMode>
    <BrowserRouter>
      <CollabWrapper />
    </BrowserRouter>
  </React.StrictMode>,
  document.getElementById('root')
);
