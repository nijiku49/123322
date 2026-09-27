function renderSiteHeader() {
  const brandEl = document.getElementById('siteBrand');
  const navEl = document.getElementById('siteNav');
  if (!brandEl || !navEl) return;

  const adminToken = localStorage.getItem('flowershop_admin_token');
  const userToken = localStorage.getItem('flowershop_user_token');
  const userName = localStorage.getItem('flowershop_user_name');

  let greeting = '';
  let links = [];

  if (adminToken) {
    links = [
      { href: '/', text: 'Главная' },
      { href: '/admin', text: 'Панель администратора' },
      { href: '#', text: 'Выйти', onclick: 'siteLogout(event)' }
    ];
  } else if (userToken) {
    greeting = 'Привет, ' + userName + '!';
    links = [
      { href: '/', text: 'Главная' },
      { href: '/cart', text: 'Корзина' },
      { href: '/orders', text: 'Мои заказы' },
      { href: '/favorites', text: 'Избранное' },
      { href: '/build', text: 'Собрать свой букет' },
      { href: '/my-bouquets', text: 'Мои букеты' },
      { href: '#', text: 'Выйти', onclick: 'siteLogout(event)' }
    ];
  } else {
    links = [
      { href: '/', text: 'Главная' },
      { href: '/register', text: 'Регистрация' },
      { href: '/login', text: 'Вход' }
    ];
  }

  brandEl.innerHTML =
    '<a href="/" class="brand">flowers</a>' +
    (greeting ? '<p class="greeting">' + greeting + '</p>' : '');

  navEl.innerHTML = links
    .map(function (l) {
      return '<a href="' + l.href + '"' + (l.onclick ? ' onclick="' + l.onclick + '"' : '') + '>&gt; ' + l.text + '</a>';
    })
    .join('');
}

function siteLogout(event) {
  event.preventDefault();
  localStorage.removeItem('flowershop_admin_token');
  localStorage.removeItem('flowershop_admin_email');
  localStorage.removeItem('flowershop_user_token');
  localStorage.removeItem('flowershop_user_name');
  window.location.href = '/';
}
