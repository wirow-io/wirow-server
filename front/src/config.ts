export class Config {
  static env = 'ENVIRONMENT';
  static port = 'APP_PORT';
  static host = 'APP_HOST';
  static buildTime = 'BUILD_TIME';
  static email = 'info@softmotions.com';
  static debugModules = 'DEBUG_MODULES';
  static minChromeVersion = 'MIN_CHROME_VERSION';
  static minFirefoxVersion = 'MIN_FIREFOX_VERSION';
  static minSafariVersion = 'MIN_SAFARI_VERSION';
  static minBlinkVersion = 'MIN_BLINK_VERSION';
  static minGeckoVersion = 'MIN_GECKO_VERSION';
  static minWebKitVersion = 'MIN_WEBKIT_VERSION';
  static enableWhiteboard = !!parseInt('ENABLE_WHITEBOARD');
  static enableRecording = !!parseInt('ENABLE_RECORDING');
  static limitRegisteredUsers = parseInt('LIMIT_REGISTERED_USERS') || null;

  static get production(): boolean {
    return Config.env === 'production';
  }

  static get timeout(): number | undefined {
    return Config.env === 'production' ? 30 : 0; // 30 sec for prod
  }
}
