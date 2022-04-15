export interface AdminUser {
  uuid: string;
  name: string;
  perms: string;
  email?: string;
  notes?: string;
}
