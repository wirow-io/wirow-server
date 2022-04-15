const emailRx = /^[-!#$%&'*+/0-9=?A-Z^_a-z`{|}~](\.?[-!#$%&'*+/0-9=?A-Z^_a-z`{|}~])*@[a-zA-Z0-9](-*\.?[a-zA-Z0-9])*\.[a-zA-Z](-?[a-zA-Z0-9])+$/;

export function validateEmail(email: string): boolean {
  if (!email || email.length > 256 || !emailRx.test(email)) {
    return false;
  }
  const emailParts = email.split('@');
  return (
    emailParts[0].length < 64 &&
    !emailParts[1].split('.').some(function (part) {
      return part.length > 63;
    })
  );
}
